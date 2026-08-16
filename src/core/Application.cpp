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
#include "AudioManager.h"
#include "CameraManager.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "EngineMath.h"
#include "script/ScriptEngine.h"
#include "editor/ContentBrowserPanel.h"
#include "editor/ConsolePanel.h"
#include "editor/InspectorPanel.h"
#include "editor/MaterialPanel.h"
#include "editor/HistoryPanel.h"
#include "editor/ViewportLayoutPanel.h"
#include "editor/ProfilerPanel.h"
#include "editor/LandscapePanel.h"
#include "editor/TimelinePanel.h"
#include "core/CommandHistory.h"
#include "core/Console.h"
#include "core/AssetImporter.h"
#include "core/Landscape.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

static const double TARGET_FRAME_TIME = 1.0 / 60.0;

// Bottom status bar height (logical pixels). The WorkspaceManager reserves
// this strip from the dock host window so the bar and the dockspace never
// overlap; DrawStatusBar pins its own window to the same strip.
static const float kStatusBarHeight = 24.0f;

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
    , m_audio(nullptr)
    , m_cameras(nullptr)
    , m_script_editor(nullptr)
    , m_command_palette(nullptr)
    , m_settings_panel(nullptr)
    , m_content_browser(nullptr)
    , m_console_panel(nullptr)
    , m_inspector_panel(nullptr)
    , m_history(nullptr)
    , m_history_panel(nullptr)
    , m_landscape_panel(nullptr)
    , m_timeline_panel(nullptr)
    , m_viewport_target(nullptr)
    , m_viewport_target_w(0)
    , m_viewport_target_h(0)
    , m_camera_preview(nullptr)
    , m_camera_preview_w(0)
    , m_camera_preview_h(0)
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
    , m_material_panel_was_visible(true)
    , m_history_panel_was_visible(true)
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
                              float near_p, int w, int h, Vec3 a, Vec3 b,
                              int *draw_calls = nullptr)
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

    if (draw_calls)
        ++(*draw_calls);
    SDL_RenderDrawLine(renderer, ax, ay, bx, by);
}

// Draw a world-space AABB as a wireframe box (12 edges), using the same
// projected-line path with near-plane clipping as the grid and mesh wireframes.
static void DrawWorldAABB(SDL_Renderer *renderer, const Mat4 &view_proj, float near_p,
                          int w, int h, const Vec3 &min, const Vec3 &max,
                          Uint8 r, Uint8 g, Uint8 b,
                          int *draw_calls = nullptr)
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
                          c[EDGES[i][0]], c[EDGES[i][1]], draw_calls);
}

static void RenderGroundGrid(SDL_Renderer *renderer, const Mat4 &view_proj,
                             float near_p, int w, int h,
                             int *draw_calls = nullptr)
{
    const float extent = 20.0f;

    // Minor grid lines on the y=0 (XZ) plane; the axis lines are drawn below.
    SDL_SetRenderDrawColor(renderer, 45, 45, 55, 255);
    for (int k = -(int)extent; k <= (int)extent; ++k)
    {
        if (k == 0)
            continue;
        DrawProjectedLine(renderer, view_proj, near_p, w, h,
                          { (float)k, 0.0f, -extent }, { (float)k, 0.0f, extent },
                          draw_calls);
        DrawProjectedLine(renderer, view_proj, near_p, w, h,
                          { -extent, 0.0f, (float)k }, { extent, 0.0f, (float)k },
                          draw_calls);
    }

    // Highlighted world axes: X = red, Z = blue.
    SDL_SetRenderDrawColor(renderer, 220, 70, 70, 255);
    DrawProjectedLine(renderer, view_proj, near_p, w, h,
                      { -extent, 0.0f, 0.0f }, { extent, 0.0f, 0.0f },
                      draw_calls);
    SDL_SetRenderDrawColor(renderer, 70, 110, 230, 255);
    DrawProjectedLine(renderer, view_proj, near_p, w, h,
                      { 0.0f, 0.0f, -extent }, { 0.0f, 0.0f, extent },
                      draw_calls);
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

// Phase 34 landscape brush cursor: the viewport override that replaces the
// transform gizmo while sculpting. Draws a projected brush-sphere ring pair
// (outer + inner cap ring) on the surface at the brush center, a depth pole
// down to the base plane, and a center cross, so the stroke footprint reads
// clearly while painting terrain.
static void DrawLandscapeBrushCursor(SDL_Renderer *renderer,
                                     const Mat4 &view_proj, float near_p,
                                     int w, int h, const Vec3 &center,
                                     float radius, int *draw_calls)
{
    if (radius <= 0.0f)
        return;
    const Vec3 up{ 0.0f, 1.0f, 0.0f };
    const Vec3 ref = (std::fabs(up.x) < 0.9f) ? Vec3{ 1.0f, 0.0f, 0.0f }
                                              : Vec3{ 0.0f, 1.0f, 0.0f };
    const Vec3 u = Vec3Normalize(Vec3Cross(ref, up));
    const Vec3 v = Vec3Cross(up, u);
    const int SEG = 48;

    for (int pass = 0; pass < 2; ++pass)
    {
        const float rr = (pass == 0) ? radius : radius * 0.65f;
        SDL_SetRenderDrawColor(renderer, 110, 200, 255, 255);
        int px = 0, py = 0;
        bool have = false;
        for (int i = 0; i <= SEG; ++i)
        {
            const float a = (float)i / (float)SEG * 6.2831853f;
            const Vec3 pt = Vec3Add(center,
                Vec3Add(Vec3Scale(u, std::cos(a) * rr),
                        Vec3Scale(v, std::sin(a) * rr)));
            int sx, sy;
            float depth;
            if (!ProjectToScreen(view_proj, near_p, w, h, pt, sx, sy, depth))
            {
                have = false;
                continue;
            }
            if (have)
            {
                if (draw_calls)
                    ++(*draw_calls);
                SDL_RenderDrawLine(renderer, px, py, sx, sy);
            }
            px = sx;
            py = sy;
            have = true;
        }
    }

    // Depth pole down to the base plane + center cross.
    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    DrawProjectedLine(renderer, view_proj, near_p, w, h,
                      center, { center.x, center.y - radius, center.z },
                      draw_calls);
    const float arm = radius * 0.14f;
    DrawProjectedLine(renderer, view_proj, near_p, w, h,
                      { center.x - arm, center.y, center.z },
                      { center.x + arm, center.y, center.z }, draw_calls);
    DrawProjectedLine(renderer, view_proj, near_p, w, h,
                      { center.x, center.y - arm, center.z },
                      { center.x, center.y + arm, center.z }, draw_calls);
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

// Resolved directional light used by the shading pass. `dir` is the normalized
// world-space direction the light TRAVELS; shading uses `-dir` (toward the
// light) so a surface is lit when its normal faces the source. `color` tints
// the diffuse term, `intensity` scales it, and `ambient` is the view-independent
// floor. The shadow group drives the ray-cast directional shadow attenuation.
struct RenderLight
{
    Vec3 dir{ 0.0f, -1.0f, 0.0f };
    Vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float ambient = 0.25f;
    float shadow_strength = 0.6f;
    float shadow_bias = 0.05f;
    float shadow_distance = 30.0f;
};

// World-space AABB blocker used by the directional shadow ray cast. The entity
// reference lets a surface ignore its own volume (no self-shadowing).
struct WorldAABB
{
    Vec3 min{ 0.0f, 0.0f, 0.0f };
    Vec3 max{ 0.0f, 0.0f, 0.0f };
    const Entity *entity = nullptr;
};

// Directional shadow factor for one triangle: a ray from the biased surface
// point toward the light is tested against every other entity's world AABB.
// The occlusion darkens by `shadow_strength` and fades out as the blocker
// moves toward `shadow_distance`. Returns 1.0 when the surface is unshadowed.
static float DirectionalShadowFactor(const RenderLight &light, const Vec3 &centroid,
                                     const Vec3 &normal,
                                     const std::vector<WorldAABB> &occluders,
                                     const Entity *self)
{
    const Vec3 origin = Vec3Add(centroid, Vec3Scale(normal, light.shadow_bias));
    const Vec3 toward_light = Vec3Scale(light.dir, -1.0f);
    const float max_dist = std::max(light.shadow_distance, 1e-6f);
    float t_near, t_far;
    for (const WorldAABB &box : occluders)
    {
        if (box.entity == self)
            continue;
        if (!RayAABB(origin, toward_light, box.min, box.max, t_near, t_far))
            continue;
        if (t_far <= 0.0f || t_near > light.shadow_distance)
            continue;  // blocker behind the surface or beyond the fade distance
        const float t = std::max(t_near, 0.0f);
        const float fade = 1.0f - t / max_dist;
        return 1.0f - light.shadow_strength * fade;
    }
    return 1.0f;
}

// Project + shade one mesh into screen-space FillTri entries. `color` is the
// RGBA albedo tint (already resolved from the entity's material asset when
// assigned); `uvs` (parallel to positions) and `texture` are used together to
// apply the diffuse map, otherwise flat normal shading is emitted. `lights`
// drive the shading (diffuse + ambient + directional shadow); with no active
// light the surface keeps its flat albedo.
static void EmitEntityTris(std::vector<FillTri> &tris, const Mesh &mesh,
                           const Mat4 &world, const Mat4 &view_proj, float near_p,
                           int w, int h, const float color[4],
                           SDL_Texture *texture, const std::vector<Vec2> *uvs,
                           const std::vector<RenderLight> &lights,
                           const std::vector<WorldAABB> &occluders,
                           const Entity *self)
{
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

        // Directional shading: diffuse from the world-space face normal, an
        // ambient floor, and a ray-cast directional shadow. Every active light
        // contributes additively; the shadow term only applies to lit faces.
        Vec3 e1 = Vec3Sub(b, a);
        Vec3 e2 = Vec3Sub(c, a);
        Vec3 n = Vec3Normalize(Vec3Cross(e1, e2));
        Vec3 centroid = Vec3Scale(Vec3Add(Vec3Add(a, b), c), 1.0f / 3.0f);

        float shade_r = 1.0f, shade_g = 1.0f, shade_b = 1.0f;
        if (!lights.empty())
        {
            shade_r = shade_g = shade_b = 0.0f;
            for (const RenderLight &l : lights)
            {
                float diffuse = std::max(0.0f, Vec3Dot(n, Vec3Scale(l.dir, -1.0f))) * l.intensity;
                float shadow = 1.0f;
                if (diffuse > 0.0f && l.shadow_strength > 0.0f && !occluders.empty())
                    shadow = DirectionalShadowFactor(l, centroid, n, occluders, self);
                float factor = l.ambient + (1.0f - l.ambient) * diffuse * shadow;
                shade_r += factor * l.color.x;
                shade_g += factor * l.color.y;
                shade_b += factor * l.color.z;
            }
        }

        FillTri t;
        t.depth = (da + db + dc) / 3.0f;
        t.x0 = ax; t.y0 = ay;
        t.x1 = bx; t.y1 = by;
        t.x2 = cx; t.y2 = cy;
        t.u0 = t.v0 = t.u1 = t.v1 = t.u2 = t.v2 = 0.0f;
        t.r = (Uint8)std::min(255.0f, base_r * shade_r);
        t.g = (Uint8)std::min(255.0f, base_g * shade_g);
        t.b = (Uint8)std::min(255.0f, base_b * shade_b);
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
                          SDL_Texture *texture, int *draw_calls = nullptr)
{
    if (verts.empty())
        return;
    // Chunked so very large meshes stay well under any renderer vertex limit.
    static const size_t MAX_VERTS = 6000;
    size_t offset = 0;
    while (offset < verts.size())
    {
        size_t count = std::min(MAX_VERTS, verts.size() - offset);
        if (draw_calls)
            ++(*draw_calls);
        SDL_RenderGeometry(renderer, texture, verts.data() + offset, (int)count, nullptr, 0);
        offset += count;
    }
    verts.clear();
}

static void RenderMeshWireframe(SDL_Renderer *renderer, const Mat4 &view_proj,
                                float near_p, int w, int h, const Mat4 &world,
                                const Mesh &mesh, const float color[3], bool brighten,
                                int *draw_calls = nullptr)
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
        DrawProjectedLine(renderer, view_proj, near_p, w, h, a, b2, draw_calls);
    }
}

static void DrawTriangles(SDL_Renderer *renderer, std::vector<FillTri> &tris, int w, int h,
                          int *draw_calls = nullptr)
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
            FlushTriBatch(renderer, verts, active_texture, draw_calls);
            active_texture = t.texture;
        }
        SDL_Color col = { t.r, t.g, t.b, 255 };
        verts.push_back({ { (float)t.x0, (float)t.y0 }, col, { t.u0, t.v0 } });
        verts.push_back({ { (float)t.x1, (float)t.y1 }, col, { t.u1, t.v1 } });
        verts.push_back({ { (float)t.x2, (float)t.y2 }, col, { t.u2, t.v2 } });
        if (verts.size() >= 6000)
            FlushTriBatch(renderer, verts, active_texture, draw_calls);
    }
    FlushTriBatch(renderer, verts, active_texture, draw_calls);
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
        // Refresh every entity's local AABB from its resolved mesh geometry
        // (Mesh::bounds_min/max). The mesh can change through the editor or
        // a scene load, so the component is recomputed each frame to stay a
        // true mirror of the geometry used for picking and collision.
        // Procedural landscapes rebuild their generated mesh here when a
        // sculpt stroke dirtied it, so rendering always sees fresh geometry.
        for (auto &entity_ptr : m_scene->GetEntities())
        {
            Entity &entity = *entity_ptr;
            const Mesh *mesh = nullptr;
            if (entity.landscape.enabled)
            {
                if (entity.landscape.mesh_dirty)
                    LandscapeRebuildMesh(entity.landscape);
                mesh = entity.landscape.mesh.get();
            }
            else
            {
                std::string mesh_error;
                mesh = ResolveMesh(entity, mesh_error);
            }
            if (mesh)
            {
                entity.bounds.local_min = mesh->bounds_min;
                entity.bounds.local_max = mesh->bounds_max;
            }
        }

        // Multi-viewport pass (Phase 27): walk the camera stack bottom-up by
        // z-order and render each enabled entry into its own region of the
        // shared target, so higher-z entries overlay lower ones. Each pass
        // resolves the entry's camera pose and renders the scene from it; the
        // primary entry additionally draws the editor overlays (selection,
        // bounds boxes, gizmo) so picking stays unambiguous. A disabled
        // primary falls back to the topmost enabled entry (PrimaryIndex).
        const int primary_idx = m_cameras->PrimaryIndex();
        for (size_t idx : m_cameras->DrawOrder())
        {
            const CameraEntry *entry = m_cameras->Get(idx);
            if (!entry || !entry->enabled)
                continue;

            int rx, ry, rw, rh;
            if (!CameraManager::RectToPixels(*entry, w, h, rx, ry, rw, rh))
                continue;

            EditorCamera pose;
            if (!ResolveCameraPose(*entry, pose))
                continue;

            // Near/far clipping come from the source camera entity when the
            // entry is scene-sourced; the editor camera has no clip planes of
            // its own, so the active camera entity's planes are used instead.
            float near_p = 0.1f, far_p = 100.0f;
            if (entry->type == CameraSourceType::SceneEntity)
            {
                if (Entity *e = m_scene->GetEntityById(entry->entity_id))
                {
                    near_p = e->camera.near_plane;
                    far_p  = e->camera.far_plane;
                }
            }
            else if (Entity *cam = FindActiveCamera())
            {
                near_p = cam->camera.near_plane;
                far_p  = cam->camera.far_plane;
            }

            float aspect = (float)rw / (float)rh;
            Mat4 view_proj;
            if (!BuildViewProjFromPose(pose, near_p, far_p, aspect, view_proj))
                continue;

            const bool is_primary = ((int)idx == primary_idx);
            Entity *skip = is_primary ? GetPrimarySkipEntity() : nullptr;
            RenderScenePass(renderer, view_proj, near_p, rw, rh, skip, m_draw_calls);

            if (is_primary && m_state == EngineState::Editor && m_gizmo)
                RenderEditorOverlay(renderer, view_proj, pose, near_p, rw, rh,
                                    m_draw_calls);
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
}

// Render the shared scene (ground grid, solid fills, wireframe) into the
// current render target region. `skip_entity` (the primary pass's own camera)
// is hidden from both fills and occluders so the player's camera never sees
// itself. This is the per-entry body of the multi-viewport render and the
// Inspector camera preview.
void Application::RenderScenePass(SDL_Renderer *renderer, const Mat4 &view_proj,
                                  float near_p, int w, int h, Entity *skip_entity,
                                  int &draw_calls)
{
    if (!m_scene)
        return;

    // Ground-plane grid first so entities draw on top of it.
    if (m_overlay.grid)
        RenderGroundGrid(renderer, view_proj, near_p, w, h, &draw_calls);

    // Phase 29 render modes: Lit shades from the scene lights, Wireframe
    // skips solid fills entirely, and Unlit skips the light loop so surfaces
    // fall back to flat albedo (and drops the wireframe pass).
    const bool draw_fills = (m_overlay.render_mode != ViewportRenderMode::Wireframe);
    const bool use_lighting = (m_overlay.render_mode != ViewportRenderMode::Unlit);
    const bool draw_wire = (m_overlay.render_mode != ViewportRenderMode::Unlit);

    // Gather the scene's active directional lights. With none active
    // the surfaces render at flat albedo (the shading loop falls back).
    std::vector<RenderLight> lights;
    if (use_lighting)
    {
        for (auto &entity_ptr : m_scene->GetEntities())
        {
            const Entity &e = *entity_ptr;
            if (!e.light.active)
                continue;
            RenderLight l;
            Vec3 dir = Vec3Normalize({ e.light.direction[0],
                                       e.light.direction[1],
                                       e.light.direction[2] });
            if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f)
                dir = { 0.0f, -1.0f, 0.0f };
            l.dir = dir;
            l.color = { e.light.color[0], e.light.color[1], e.light.color[2] };
            l.intensity = e.light.intensity;
            l.ambient = e.light.ambient;
            l.shadow_strength = e.light.shadow_strength;
            l.shadow_bias = e.light.shadow_bias;
            l.shadow_distance = e.light.shadow_distance;
            lights.push_back(l);
        }
    }

    // World AABBs of the visible mesh-bearing entities, used as
    // directional-shadow blockers for the ray cast in EmitEntityTris.
    std::vector<WorldAABB> occluders;
    for (auto &entity_ptr : m_scene->GetEntities())
    {
        Entity &e = *entity_ptr;
        if (&e == skip_entity || !e.material.active)
            continue;
        const Mesh *mesh = ResolveEntityMesh(e);
        if (!mesh)
            continue;
        Mat4 world = m_scene->ComputeWorldMatrix(e);
        WorldAABB box;
        TransformAABB(e.bounds.local_min, e.bounds.local_max, world, box.min, box.max);
        box.entity = &e;
        occluders.push_back(box);
    }

    // --- Pass 1: solid fills, one global painter's pass ---
    if (draw_fills)
    {
        std::vector<FillTri> tris;
        for (auto &entity_ptr : m_scene->GetEntities())
        {
            Entity &entity = *entity_ptr;
            if (&entity == skip_entity || !entity.material.active)
                continue;

            // Landscapes render their generated mesh; every other entity
            // resolves through the mesh library, surfacing load failures to
            // the status bar so a broken asset is never silently dropped.
            std::string mesh_error;
            const Mesh *mesh = entity.landscape.enabled
                ? entity.landscape.mesh.get()
                : ResolveMesh(entity, mesh_error);
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
                           tint, texture, uvs, lights, occluders, &entity);
        }
        DrawTriangles(renderer, tris, w, h, &draw_calls);
    }

    // --- Pass 2: wireframe overlay for every visible entity ---
    if (draw_wire)
    {
        for (auto &entity_ptr : m_scene->GetEntities())
        {
            Entity &entity = *entity_ptr;
            if (&entity == skip_entity || !entity.material.active)
                continue;

            const Mesh *mesh = ResolveEntityMesh(entity);
            if (!mesh)
                continue;

            Mat4 world = m_scene->ComputeWorldMatrix(entity);
            RenderMeshWireframe(renderer, view_proj, near_p, w, h, world,
                                *mesh, entity.material.color, true, &draw_calls);
        }
    }
}

// Editor-only overlays drawn into the primary viewport region: the amber
// selection outline + bounds box, the hovered entity's light-blue bounds box,
// collider volumes, and the gizmo handles. Runs with no selection so the
// hovered entity's bounds box still draws.
void Application::RenderEditorOverlay(SDL_Renderer *renderer, const Mat4 &view_proj,
                                      const EditorCamera &pose, float near_p,
                                      int w, int h, int &draw_calls)
{
    if (!m_scene || !m_selection || !m_gizmo)
        return;

    Entity *camera_entity = GetPrimarySkipEntity();

    Entity *selected = (m_selection->entity_id >= 0)
        ? m_scene->GetEntityById(m_selection->entity_id) : nullptr;

    // Selected entity: amber wireframe outline + white bounds box.
    if (m_overlay.bounds && selected && selected != camera_entity)
    {
        const Mesh *mesh = ResolveEntityMesh(*selected);
        static const float OUTLINE[3] = { 1.0f, 0.65f, 0.2f }; // amber
        if (mesh)
        {
            Mat4 world = m_scene->ComputeWorldMatrix(*selected);
            RenderMeshWireframe(renderer, view_proj, near_p, w, h,
                                world, *mesh, OUTLINE, false, &draw_calls);
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
                      sel_wmin, sel_wmax, 255, 255, 255, &draw_calls);
    }

    // Hovered entity (ray/AABB hit under the cursor): light-blue
    // bounds box, distinct from the amber selection.
    if (m_overlay.bounds)
    {
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
                              hover_wmin, hover_wmax, 110, 180, 255,
                              &draw_calls);
            }
        }
    }

    // Physics collider volumes (editor aid): solid = green,
    // trigger = cyan. Drawn from the collider's own local box
    // (center +/- extents) transformed into the world frame.
    if (m_overlay.colliders)
    {
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
                          is_trigger ? 210 : 110,
                          &draw_calls);
        }
    }

    // Directional-light gizmos (editor aid): the default light entity is
    // mesh-less, so without this it would be invisible in the scene. Draw a
    // small sun cross at the light's world position plus an arrow along its
    // direction; active lights are amber, inactive ones dim grey.
    if (m_overlay.light_gizmos)
    {
        const float arm = 0.4f;   // cross arm length (world units)
        const float reach = 2.0f; // direction arrow length
        for (auto &entity_ptr : m_scene->GetEntities())
        {
            Entity &light_entity = *entity_ptr;
            if (!light_entity.light.active)
                continue;
            if (&light_entity == camera_entity)
                continue;

            Mat4 lw = m_scene->ComputeWorldMatrix(light_entity);
            const Vec3 pos{ lw.m[12], lw.m[13], lw.m[14] };
            Vec3 dir = Vec3Normalize({ light_entity.light.direction[0],
                                       light_entity.light.direction[1],
                                       light_entity.light.direction[2] });
            if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f)
                dir = { 0.0f, -1.0f, 0.0f };

            SDL_SetRenderDrawColor(renderer, 255, 205, 90, 255);
            // Sun cross (world X/Z ticks around the light position).
            DrawProjectedLine(renderer, view_proj, near_p, w, h,
                              { pos.x - arm, pos.y, pos.z }, { pos.x + arm, pos.y, pos.z },
                              &draw_calls);
            DrawProjectedLine(renderer, view_proj, near_p, w, h,
                              { pos.x, pos.y, pos.z - arm }, { pos.x, pos.y, pos.z + arm },
                              &draw_calls);
            // Direction arrow + head tick.
            const Vec3 tip{ pos.x + dir.x * reach, pos.y + dir.y * reach, pos.z + dir.z * reach };
            DrawProjectedLine(renderer, view_proj, near_p, w, h, pos, tip,
                              &draw_calls);
            const Vec3 side = Vec3Normalize(Vec3Cross(dir, { 0.0f, 1.0f, 0.0f }));
            if (side.x != 0.0f || side.y != 0.0f || side.z != 0.0f)
            {
                const float head = 0.35f;
                const Vec3 perp{ side.x * head, side.y * head, side.z * head };
                DrawProjectedLine(renderer, view_proj, near_p, w, h,
                                  tip, { tip.x + perp.x, tip.y + perp.y, tip.z + perp.z },
                                  &draw_calls);
                DrawProjectedLine(renderer, view_proj, near_p, w, h,
                                  tip, { tip.x - perp.x, tip.y - perp.y, tip.z - perp.z },
                                  &draw_calls);
            }
        }
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
    gf.cam_pos = pose.position;
    gf.cam_pitch = pose.pitch;
    gf.cam_yaw = pose.yaw;
    gf.cam_fov = pose.fov;
    gf.near_p = near_p;
    gf.view_proj = view_proj;
    gf.dt = 0.0f;
    gf.snap_translation = m_snap.translation;
    gf.snap_rotation = m_snap.rotation;
    gf.snap_scale = m_snap.scale;
    gf.snap_active = m_snap.enabled || ImGui::GetIO().KeyCtrl;

    // Phase 34 viewport override: in Landscape Mode the transform gizmo is
    // replaced by the sculpt brush cursor — a projected brush-sphere ring
    // tracking the mouse on the terrain (hit computed by UpdateLandscapeBrush
    // earlier in the frame). The gizmo stays suppressed while sculpting.
    if (IsLandscapeSculptMode())
    {
        if (m_landscape_brush_valid)
        {
            DrawLandscapeBrushCursor(renderer, view_proj, near_p, w, h,
                                     m_landscape_brush_center,
                                     m_landscape_brush.radius, &draw_calls);
        }
    }
    else if (m_overlay.gizmo)
    {
        m_gizmo->Draw(renderer, gf);
    }
}

Entity *Application::FindActiveCamera() const
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

    // The pose to render from: the primary viewport entry. In the default
    // layout that is the editor camera in editor mode, the active gameplay
    // camera in play mode, or a smooth blend while transitioning.
    const CameraEntry *entry = m_cameras->Get(m_cameras->PrimaryIndex());
    if (!entry)
        return false;

    EditorCamera pose;
    if (!ResolveCameraPose(*entry, pose))
        return false;
    cam_pos = pose.position;
    fov     = pose.fov;
    pitch   = pose.pitch;
    yaw     = pose.yaw;

    near_p = 0.1f;
    far_p  = 100.0f;
    // Near/far clipping stay properties of the gameplay camera entity (the
    // editor camera has no clip planes of its own).
    if (Entity *camera_entity = FindActiveCamera())
    {
        near_p = camera_entity->camera.near_plane;
        far_p  = camera_entity->camera.far_plane;
    }

    float aspect = (float)m_viewport_target_w / (float)m_viewport_target_h;
    return BuildViewProjFromPose(pose, near_p, far_p, aspect, view_proj);
}

// View = RotX(-pitch) * RotY(-yaw) * Translate(-cam_pos): the inverse
// of the camera's world orientation. Yaw first about world up, then
// pitch about the camera's local right axis, so roll stays locked to
// zero for any yaw/pitch combination.
bool Application::BuildViewProjFromPose(const EditorCamera &pose, float near_p,
                                        float far_p, float aspect,
                                        Mat4 &view_proj)
{
    if (aspect <= 0.0f || near_p <= 0.0f || far_p <= near_p)
        return false;

    Mat4 view = Mat4Mul(
        Mat4RotateX(-pose.pitch),
        Mat4Mul(Mat4RotateY(-pose.yaw), Mat4Translate(-pose.position.x, -pose.position.y, -pose.position.z))
    );

    Mat4 proj = Mat4Perspective(pose.fov, aspect, near_p, far_p);
    view_proj = Mat4Mul(proj, view);
    return true;
}

// Resolve the pose an entry renders with. Editor entries use the active
// camera pose (editor camera, or the play-mode gameplay camera with any
// Play/Stop blend applied); SceneEntity entries read the referenced camera
// entity. Returns false when the source cannot be resolved.
bool Application::ResolveCameraPose(const CameraEntry &entry, EditorCamera &out)
{
    if (entry.type == CameraSourceType::SceneEntity)
    {
        if (!m_scene)
            return false;
        Entity *e = m_scene->GetEntityById(entry.entity_id);
        if (!e)
            return false;
        return CaptureSceneCamera(*e, out);
    }
    return GetActiveCameraPose(out);
}

// Read any camera entity's pose (position from its world matrix,
// orientation/fov from its CameraComponent). Falls back to a sensible default
// pose; returns whether the entity actually has a usable camera component.
bool Application::CaptureSceneCamera(Entity &camera_entity, EditorCamera &out)
{
    out.position = { 0.0f, 2.0f, 8.0f };
    out.pitch = -14.0f;
    out.yaw = 0.0f;
    out.fov = 60.0f;
    if (!m_scene || camera_entity.camera.fov <= 0.0f)
        return false;

    Mat4 cam_world = m_scene->ComputeWorldMatrix(camera_entity);
    out.position.x = cam_world.m[12];
    out.position.y = cam_world.m[13];
    out.position.z = cam_world.m[14];
    out.pitch = camera_entity.camera.pitch;
    out.yaw   = camera_entity.camera.yaw;
    if (camera_entity.camera.fov > 1.0f)
        out.fov = camera_entity.camera.fov;
    return true;
}

// The entity hidden from the primary viewport pass: the camera entity the
// primary entry renders from, so the player's camera never sees itself.
Entity *Application::GetPrimarySkipEntity() const
{
    const CameraEntry *entry = m_cameras->Get(m_cameras->PrimaryIndex());
    if (entry && entry->type == CameraSourceType::SceneEntity && m_scene)
        return m_scene->GetEntityById(entry->entity_id);
    return FindActiveCamera();
}

// Pixel rect of the primary viewport entry inside the render target, used to
// map mouse position to world space. Falls back to the whole target.
bool Application::GetPrimaryViewportRect(int &px, int &py, int &pw, int &ph) const
{
    const CameraEntry *entry = m_cameras->Get(m_cameras->PrimaryIndex());
    if (entry &&
        CameraManager::RectToPixels(*entry, m_viewport_target_w, m_viewport_target_h,
                                    px, py, pw, ph))
        return true;
    px = 0; py = 0; pw = m_viewport_target_w; ph = m_viewport_target_h;
    return pw > 0 && ph > 0;
}

// (Re)create the Inspector camera-preview target. The preview is rendered
// lazily at whatever size the Inspector asks for and recreated on demand.
void Application::RecreateCameraPreview(int width, int height)
{
    if (!m_window || width <= 0 || height <= 0)
        return;
    if (m_camera_preview && m_camera_preview_w == width && m_camera_preview_h == height)
        return;
    if (m_camera_preview)
    {
        SDL_DestroyTexture(m_camera_preview);
        m_camera_preview = nullptr;
    }
    SDL_Renderer *renderer = m_window->GetNativeRenderer();
    m_camera_preview = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, width, height);
    m_camera_preview_w = width;
    m_camera_preview_h = height;
}

// Render the selected camera entity into the preview target each editor frame
// so the Inspector can show its live feed. The preview deliberately reuses the
// scene pass (grid, lights, fills, wireframe) but never the editor overlays.
void Application::RenderCameraPreview()
{
    if (!m_camera_preview || !m_scene || m_state != EngineState::Editor)
        return;

    Entity *preview_cam = nullptr;
    if (m_selection && m_selection->entity_id >= 0)
        preview_cam = m_scene->GetEntityById(m_selection->entity_id);
    if (!preview_cam || preview_cam->camera.fov <= 0.0f)
        return;

    SDL_Renderer *renderer = m_window->GetNativeRenderer();
    if (SDL_SetRenderTarget(renderer, m_camera_preview) != 0)
        return;

    SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
    SDL_RenderClear(renderer);

    EditorCamera pose;
    if (CaptureSceneCamera(*preview_cam, pose))
    {
        int w = m_camera_preview_w;
        int h = m_camera_preview_h;
        float aspect = (float)w / (float)h;
        Mat4 view_proj;
        if (BuildViewProjFromPose(pose, preview_cam->camera.near_plane,
                                  preview_cam->camera.far_plane, aspect, view_proj))
        {
            // The previewed camera is skipped so it never sees itself.
            RenderScenePass(renderer, view_proj, preview_cam->camera.near_plane,
                            w, h, preview_cam, m_draw_calls);
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
}

// Read the active gameplay camera entity's pose (position from its world
// matrix, orientation/fov from its CameraComponent). Falls back to a sensible
// default pose when the scene has no camera entity; returns whether a camera
// entity was actually found.
bool Application::CaptureGameplayCamera(EditorCamera &out)
{
    out.position = { 0.0f, 2.0f, 8.0f };
    out.pitch = -14.0f;
    out.yaw = 0.0f;
    out.fov = 60.0f;

    Entity *camera_entity = FindActiveCamera();
    if (!camera_entity)
        return false;

    Mat4 cam_world = m_scene->ComputeWorldMatrix(*camera_entity);
    out.position.x = cam_world.m[12];
    out.position.y = cam_world.m[13];
    out.position.z = cam_world.m[14];
    out.pitch = camera_entity->camera.pitch;
    out.yaw   = camera_entity->camera.yaw;
    if (camera_entity->camera.fov > 1.0f)
        out.fov = camera_entity->camera.fov;
    return true;
}

// The pose the viewport should render with this frame. During a Play/Stop
// blend the view follows the eased transition; otherwise it is the editor
// camera (editor mode) or the active gameplay camera (play mode).
bool Application::GetActiveCameraPose(EditorCamera &out)
{
    if (m_camera_transition.phase != CameraTransitionPhase::None)
    {
        out = CameraBlend(m_camera_transition.from, m_camera_transition.to,
                          m_camera_transition.t);
        return true;
    }

    if (m_state == EngineState::Play)
        return CaptureGameplayCamera(out);

    out = m_editor_camera;
    return true;
}

// Start a Play/Stop camera blend. The `from` pose is whatever the viewport is
// currently showing (so a second toggle mid-blend stays continuous); `to` is
// the target camera captured at blend time.
void Application::BeginCameraTransition(CameraTransitionPhase phase)
{
    GetActiveCameraPose(m_camera_transition.from);
    m_camera_transition.to = m_editor_camera;
    if (phase == CameraTransitionPhase::ToGameplay)
        CaptureGameplayCamera(m_camera_transition.to);
    m_camera_transition.phase = phase;
    m_camera_transition.t = 0.0f;
}

// Advance an in-flight blend by dt; the phase clears once it reaches the end.
void Application::UpdateCameraTransition(float dt)
{
    if (m_camera_transition.phase == CameraTransitionPhase::None)
        return;
    m_camera_transition.t += dt / kCameraTransitionDuration;
    if (m_camera_transition.t >= 1.0f)
    {
        m_camera_transition.t = 1.0f;
        m_camera_transition.phase = CameraTransitionPhase::None;
    }
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

// The mesh that renders / picks an entity: procedural landscapes carry their
// generated mesh directly (rebuilt on demand by RenderViewportTarget), every
// other entity resolves through the mesh library like ResolveMesh.
const Mesh *Application::ResolveEntityMesh(const Entity &entity)
{
    if (entity.landscape.enabled && entity.landscape.mesh)
        return entity.landscape.mesh.get();
    std::string error;
    return ResolveMesh(entity, error);
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
        PushToast("Scene saved");
        ConsoleInfo("Scene saved: " + m_scene_path);
    }
    else
    {
        m_scene_status = "Save failed: " + error;
        PushToast("Save failed: " + error);
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
        PushToast("Scene loaded");
        ConsoleInfo("Scene loaded: " + filepath + " (" +
                    std::to_string(m_scene->GetEntities().size()) + " entities)");
    }
    else
    {
        m_scene_status = "Open failed: " + error;
        PushToast("Open failed: " + error);
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
        PushToast("New scene: " + m_scene_manager->ActiveName());
        ConsoleInfo("New scene created: " + m_scene_manager->ActiveName());
    }
    else
    {
        m_scene_status = "New scene failed: " + error;
        PushToast("New scene failed: " + error);
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

    // Smoothly blend the view from the free-fly editor camera to the active
    // gameplay camera while play mode warms up (scripts bind below).
    BeginCameraTransition(CameraTransitionPhase::ToGameplay);

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

    // Audio components flagged auto_play start their sample when play begins
    // (looping or one-shot, at the component's volume). Playback stops again on
    // exit so no editor-queued sound bleeds into the next session.
    if (m_audio)
    {
        for (auto &entity_ptr : m_scene->GetEntities())
        {
            Entity &entity = *entity_ptr;
            if (entity.audio.auto_play && !entity.audio.path.empty())
                m_audio->Play(entity.audio.path, entity.audio.volume,
                              entity.audio.loop);
        }
    }

    m_state = EngineState::Play;
    PushToast("Play mode");
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

    // Stop every channel the play session started (auto-play loops would
    // otherwise keep sounding in the editor after Stop).
    if (m_audio)
        m_audio->StopAll();

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
    PushToast("Stopped play mode");

    // Blend the view back from the (restored) gameplay camera to the free-fly
    // editor camera, so leaving play glides back to where the user was editing.
    BeginCameraTransition(CameraTransitionPhase::ToEditor);

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
    // Restore the workspace-appropriate chrome (e.g. hide the viewport again in
    // the Sequencing workspace) that the isolated play view overrode.
    SyncWorkspaceSideEffects(m_workspace_manager.GetWorkspace());
}

void Application::SavePlayModePanelState()
{
    m_play_panel_saved = true;
    m_script_editor_was_visible = m_script_editor ? m_script_editor->IsVisible() : false;
    m_content_browser_was_visible = m_content_browser ? m_content_browser->IsVisible() : false;
    m_console_was_visible = m_console_panel ? m_console_panel->IsVisible() : false;
    m_inspector_was_visible = m_inspector_panel ? m_inspector_panel->IsVisible() : false;
    m_material_panel_was_visible = m_material_panel ? m_material_panel->IsVisible() : false;
    m_history_panel_was_visible = m_history_panel ? m_history_panel->IsVisible() : false;

    if (m_script_editor)
        m_script_editor->SetVisible(false);
    if (m_content_browser)
        m_content_browser->SetVisible(false);
    if (m_console_panel)
        m_console_panel->SetVisible(false);
    if (m_inspector_panel)
        m_inspector_panel->SetVisible(false);
    if (m_material_panel)
        m_material_panel->SetVisible(false);
    if (m_history_panel)
        m_history_panel->SetVisible(false);
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
    if (m_material_panel)
        m_material_panel->SetVisible(m_material_panel_was_visible);
    if (m_history_panel)
        m_history_panel->SetVisible(m_history_panel_was_visible);
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
    // The spawn is registered in the undo history: first Undo deletes the
    // clone, Redo re-creates it from the serialized capture.
    Entity *clone = SceneSerializer::DuplicateEntity(*m_scene, *source, source->parent);
    if (clone)
    {
        if (m_history)
            m_history->PushSpawn(*clone, ("Duplicate '" + source->tag.tag + "'").c_str());
        m_selection->entity_id = clone->id;
        m_selection->entity_name = clone->tag.tag;
        m_scene_status = "Duplicated '" + clone->tag.tag + "'";
        PushToast("Duplicated '" + clone->tag.tag + "'");
    }
}

Entity *Application::SpawnPrimitive(const char *label, const char *mesh_path,
                                    const char *material_path)
{
    if (!m_scene || !label)
        return nullptr;

    Entity &created = m_scene->CreateEntity(label);
    if (mesh_path)
        created.mesh.path = mesh_path;
    if (material_path)
        created.material.material_path = material_path;

    // Offset the new object in front of the editor camera so it is visible
    // even when the origin is occluded.
    const float yaw = m_editor_camera.yaw * 3.1415926535f / 180.0f;
    created.transform.position[0] = m_editor_camera.position.x - std::sin(yaw) * 3.0f;
    created.transform.position[1] = m_editor_camera.position.y + 0.5f;
    created.transform.position[2] = m_editor_camera.position.z - std::cos(yaw) * 3.0f;

    if (m_history)
        m_history->PushSpawn(created, ("Create '" + std::string(label) + "'").c_str());
    m_selection->entity_id = created.id;
    m_selection->entity_name = created.tag.tag;
    m_scene_status = "Spawned '" + std::string(label) + "'";
    PushToast("Spawned '" + std::string(label) + "'");
    return &created;
}

// Phase 34: spawn a sculptable heightfield terrain. The grid is initialized,
// meshed, tinted as ground, placed in front of the editor camera, selected and
// armed as the brush target. Undoable like any spawn.
Entity *Application::CreateLandscape()
{
    if (!m_scene)
        return nullptr;

    Entity &created = m_scene->CreateEntity("Landscape");
    created.landscape.enabled = true;
    created.landscape.resolution = 64;
    created.landscape.size = 40.0f;
    created.landscape.base_height = 0.0f;
    LandscapeInitialize(created.landscape);
    LandscapeRebuildMesh(created.landscape);
    created.material.color[0] = 0.35f;
    created.material.color[1] = 0.55f;
    created.material.color[2] = 0.33f;
    created.material.color[3] = 1.0f;

    // Center the grid a few meters in front of the editor camera so it is
    // visible on creation; the surface sits at y = base_height (0).
    const float yaw = m_editor_camera.yaw * 3.1415926535f / 180.0f;
    created.transform.position[0] =
        m_editor_camera.position.x - std::sin(yaw) * 6.0f;
    created.transform.position[2] =
        m_editor_camera.position.z - std::cos(yaw) * 6.0f;

    if (m_history)
        m_history->PushSpawn(created, "Create 'Landscape'");
    m_selection->entity_id = created.id;
    m_selection->entity_name = created.tag.tag;
    m_landscape_brush.target_id = created.id;
    m_scene_status = "Created 'Landscape'";
    PushToast("Created 'Landscape'");
    return &created;
}

bool Application::IsLandscapeSculptMode() const
{
    if (m_workspace_manager.GetWorkspace() != WorkspaceManager::Workspace::Landscape)
        return false;
    if (m_landscape_brush.target_id < 0 || !m_scene)
        return false;
    const Entity *target = m_scene->GetEntityById(m_landscape_brush.target_id);
    return target && target->landscape.enabled;
}

void Application::UpdateLandscapeBrush(const GizmoFrame &gf, float dt)
{
    m_landscape_brush_valid = false;

    Entity *target = m_scene
        ? m_scene->GetEntityById(m_landscape_brush.target_id) : nullptr;
    if (!target || !target->landscape.enabled)
    {
        // Target vanished (scene switch / delete): close any dangling stroke
        // transaction so the undo stack never sits open.
        if (m_landscape_sculpting)
        {
            m_landscape_sculpting = false;
            if (m_history)
                m_history->EndEntityEdit();
        }
        return;
    }

    if (!gf.hovered || gf.vp_width <= 1.0f || gf.vp_height <= 1.0f)
        return;

    // Build the pick ray in the same camera basis as the gizmo (matches
    // ComputeDropWorldPos): view = RotX(-pitch) * RotY(-yaw) * Translate(-pos).
    const float PI = 3.1415926535f;
    const float nx = (2.0f * gf.mouse_x / gf.vp_width - 1.0f);
    const float ny = (1.0f - 2.0f * gf.mouse_y / gf.vp_height);
    const float tan_half = std::tan(gf.cam_fov * PI / 360.0f);
    const float aspect = gf.vp_width / gf.vp_height;
    const float p = gf.cam_pitch * PI / 180.0f;
    const float y = gf.cam_yaw * PI / 180.0f;
    const float sp = std::sin(p), cp = std::cos(p);
    const float sy = std::sin(y), cy = std::cos(y);
    const Vec3 right{ cy, 0.0f, -sy };
    const Vec3 up{ sp * sy, cp, sp * cy };
    const Vec3 fwd{ cp * sy, -sp, cp * cy };
    const Vec3 dir = Vec3Normalize(Vec3Add(
        Vec3Add(Vec3Scale(right, nx * tan_half * aspect),
                Vec3Scale(up, ny * tan_half)),
        Vec3Scale(fwd, -1.0f)));

    const Mat4 world = m_scene->ComputeWorldMatrix(*target);
    Vec3 hit;
    float t;
    if (!LandscapeRaycast(target->landscape, world, gf.cam_pos, dir, t, hit))
        return;
    m_landscape_brush_valid = true;
    m_landscape_brush_center = hit;

    // Paint while LMB is held: one undo transaction per stroke.
    const bool lmb = ImGui::IsMouseDown(0);
    if (lmb && !m_landscape_sculpting)
    {
        m_landscape_sculpting = true;
        if (m_history)
            m_history->BeginEntityEdit(target->id, "Sculpt Landscape");
    }
    if (m_landscape_sculpting)
    {
        const Vec3 local = LandscapeWorldToLocal(world, hit);
        const float scale = LandscapeWorldScale(world);
        LandscapeSculpt(target->landscape, m_landscape_brush.tool, local,
                        m_landscape_brush.radius / std::max(scale, 1e-6f),
                        m_landscape_brush.strength * dt,
                        m_landscape_brush.falloff);
    }
    if (!lmb && m_landscape_sculpting)
    {
        m_landscape_sculpting = false;
        if (m_history)
            m_history->EndEntityEdit();
    }
}

// --- Phase 35 animation & timeline foundation --------------------------------

void Application::ApplyTimeline(float dt)
{
    if (m_state != EngineState::Editor || !m_scene)
        return;

    // Advance the global clock while playing: wrap around Loop, otherwise clamp
    // at the end and stop the transport on the final pose.
    if (m_timeline.playing)
    {
        m_timeline.time += dt;
        if (m_timeline.duration > 0.0f)
        {
            if (m_timeline.loop)
            {
                m_timeline.time = std::fmod(m_timeline.time, m_timeline.duration);
            }
            else if (m_timeline.time >= m_timeline.duration)
            {
                m_timeline.time = m_timeline.duration;
                m_timeline.playing = false;
            }
        }
        m_timeline_dirty = true;
    }
    if (!m_timeline_dirty)
        return;
    m_timeline_dirty = false;

    // Write the sampled pose for every entity carrying keyframes. Empty tracks
    // leave the authored value untouched.
    for (auto &entity_ptr : m_scene->GetEntities())
    {
        Entity &entity = *entity_ptr;
        const AnimationComponent &anim = entity.animation;
        if (anim.position.IsEmpty() && anim.rotation.IsEmpty() && anim.scale.IsEmpty())
            continue;
        Anim::Apply(anim, m_timeline.time, entity.transform.position,
                    entity.transform.rotation, entity.transform.scale);
    }
}

void Application::PlayPauseTimeline()
{
    m_timeline.playing = !m_timeline.playing;
    if (m_timeline.playing)
    {
        if (m_timeline.time >= m_timeline.duration && m_timeline.duration > 0.0f)
            m_timeline.time = 0.0f;  // restart when parked at the end
        m_timeline_dirty = true;
        m_scene_status = "Timeline playing";
    }
    else
    {
        m_scene_status = "Timeline paused";
    }
}

void Application::StopTimeline()
{
    m_timeline.playing = false;
    m_timeline.time = 0.0f;
    m_timeline_dirty = true;
}

void Application::ScrubTimeline()
{
    // The panel already wrote the new playhead into m_timeline.time.
    m_timeline_dirty = true;
}

Entity *Application::FindTimelineTarget() const
{
    if (!m_scene || !m_selection || m_selection->entity_id < 0)
        return nullptr;
    return m_scene->GetEntityById(m_selection->entity_id);
}

void Application::SetTimelineKeyframe(AnimProperty prop)
{
    Entity *entity = FindTimelineTarget();
    if (!entity || !m_history)
        return;

    const float t = m_timeline.time;
    m_history->BeginEntityEdit(entity->id, "Set Keyframe");
    switch (prop)
    {
        case AnimProperty::Position:
            Anim::SetKeyframe(entity->animation.position, t, entity->transform.position);
            break;
        case AnimProperty::Rotation:
            Anim::SetKeyframe(entity->animation.rotation, t, entity->transform.rotation);
            break;
        case AnimProperty::Scale:
            Anim::SetKeyframe(entity->animation.scale, t, entity->transform.scale);
            break;
    }
    // The component's duration mirrors the longest key time; stretch the global
    // timeline so a key recorded past its edge is never unreachable.
    const float duration = Anim::TrackDuration(entity->animation);
    entity->animation.duration = duration;
    if (duration > m_timeline.duration)
        m_timeline.duration = duration;
    m_history->EndEntityEdit();

    m_timeline_dirty = true;
    m_scene_status = "Recorded keyframe at " + std::to_string(t) + "s";
}

void Application::RemoveTimelineKeyframe(AnimProperty prop, float time)
{
    Entity *entity = FindTimelineTarget();
    if (!entity || !m_history)
        return;

    m_history->BeginEntityEdit(entity->id, "Remove Keyframe");
    switch (prop)
    {
        case AnimProperty::Position: Anim::RemoveKeyframe(entity->animation.position, time); break;
        case AnimProperty::Rotation: Anim::RemoveKeyframe(entity->animation.rotation, time); break;
        case AnimProperty::Scale:    Anim::RemoveKeyframe(entity->animation.scale, time);    break;
    }
    entity->animation.duration = Anim::TrackDuration(entity->animation);
    m_history->EndEntityEdit();

    m_timeline_dirty = true;
}

unsigned int Application::ApplyWorkspace(WorkspaceManager::Workspace ws)
{
    const unsigned int node = m_workspace_manager.ApplyWorkspace(ws);
    SyncWorkspaceSideEffects(ws);
    return node;
}

unsigned int Application::ResetWorkspaceDefault()
{
    const unsigned int node = m_workspace_manager.ResetToWorkspaceDefault();
    SyncWorkspaceSideEffects(m_workspace_manager.GetWorkspace());
    return node;
}

void Application::SyncWorkspaceSideEffects(WorkspaceManager::Workspace ws)
{
    // The Sequencing workspace replaces the viewport with the Timeline editor.
    if (m_viewport)
        m_viewport->SetVisible(ws != WorkspaceManager::Workspace::Timeline);
    // Leaving the Sequencing workspace stops playback so no hidden animation
    // keeps mutating transforms behind the author's back.
    if (ws != WorkspaceManager::Workspace::Timeline && m_timeline.playing)
        StopTimeline();
}

void Application::DeleteSelection()
{
    if (m_state != EngineState::Editor)
        return;
    if (!m_selection || m_selection->entity_id < 0)
        return;

    Entity *entity = m_scene->GetEntityById(m_selection->entity_id);
    if (!entity)
        return;
    const std::string name = entity->tag.tag;
    if (m_history)
        m_history->ExecuteDelete(*entity, ("Delete '" + name + "'").c_str());
    m_selection->entity_id = -1;
    m_selection->entity_name.clear();
    m_scene_status = "Deleted '" + name + "'";
    PushToast("Deleted '" + name + "'");
}

void Application::SpawnMeshEntity(const std::string &mesh_path, const Vec3 &position)
{
    if (!m_scene || mesh_path.empty())
        return;

    // Display name = file stem, e.g. "assets/meshes/gear.obj" -> "gear".
    const size_t slash = mesh_path.find_last_of('/');
    const std::string leaf = (slash == std::string::npos)
        ? mesh_path : mesh_path.substr(slash + 1);
    std::string name = leaf;
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);

    Entity &created = m_scene->CreateEntity(name.empty() ? "Mesh" : name);
    created.mesh.path = mesh_path;
    created.transform.position[0] = position.x;
    created.transform.position[1] = position.y;
    created.transform.position[2] = position.z;
    if (m_history)
        m_history->PushSpawn(created, ("Create '" + created.tag.tag + "'").c_str());
    m_selection->entity_id = created.id;
    m_selection->entity_name = created.tag.tag;
    m_scene_status = "Spawned '" + created.tag.tag + "' at drop position";
}

bool Application::ComputeDropWorldPos(float sx, float sy, float vp_w, float vp_h, Vec3 &out)
{
    Mat4 view_proj;
    Vec3 cam_pos;
    float fov, pitch, yaw, near_p, far_p;
    if (!BuildViewProj(view_proj, cam_pos, fov, pitch, yaw, near_p, far_p))
        return false;
    if (vp_w <= 0.0f || vp_h <= 0.0f)
        return false;

    // Same camera-basis ray as the gizmo controller's MakeRay (matches the
    // RotX(-pitch) * RotY(-yaw) * Translate(-pos) view), intersected with the
    // y=0 grid plane.
    const float PI = 3.1415926535f;
    float nx = (2.0f * sx / vp_w - 1.0f);
    float ny = (1.0f - 2.0f * sy / vp_h);
    float tan_half = std::tan(fov * PI / 360.0f);
    float aspect = vp_w / vp_h;
    float p = pitch * PI / 180.0f, y = yaw * PI / 180.0f;
    float sp = std::sin(p), cp = std::cos(p);
    float syv = std::sin(y), cy = std::cos(y);
    Vec3 right{ cy, 0.0f, -syv };
    Vec3 up{ sp * syv, cp, sp * cy };
    Vec3 fwd{ cp * syv, -sp, cp * cy };
    Vec3 dir = Vec3Normalize(Vec3Add(
        Vec3Add(Vec3Scale(right, nx * tan_half * aspect),
                Vec3Scale(up, ny * tan_half)),
        Vec3Scale(fwd, -1.0f)));

    if (std::fabs(dir.y) < 1e-6f)
        return false;
    float t = (0.0f - cam_pos.y) / dir.y;
    out = Vec3Add(cam_pos, Vec3Scale(dir, t));
    return true;
}

bool Application::ComputeDropWorldPosFromMouse(Vec3 &out)
{
    if (!m_viewport || m_viewport_target_w <= 0 || m_viewport_target_h <= 0)
        return false;
    ImVec2 img_min = m_viewport->GetImageMin();
    ImVec2 img_size = m_viewport->GetImageSize();
    if (img_size.x <= 1.0f || img_size.y <= 1.0f)
        return false;
    ImVec2 mouse = ImGui::GetMousePos();

    // The drop target is the primary viewport region, not the whole texture:
    // multi-viewport layouts map drops through whichever region owns the mouse.
    int px, py, pw, ph;
    GetPrimaryViewportRect(px, py, pw, ph);
    const float sx = (mouse.x - img_min.x) * ((float)m_viewport_target_w / img_size.x) - (float)px;
    const float sy = (mouse.y - img_min.y) * ((float)m_viewport_target_h / img_size.y) - (float)py;
    return ComputeDropWorldPos(sx, sy, (float)pw, (float)ph, out);
}

void Application::ProcessExternalDrops()
{
    if (m_pending_drops.empty())
        return;

    // The SDL_DROPFILE event does not carry reliable window coordinates on
    // every platform, and the ImGui panels live in logical screen space, so
    // route against the current (logical) mouse position - which is exactly
    // where the drop landed this frame.
    const ImVec2 logical = ImGui::GetIO().MousePos;

    // Dropped inside the Content Browser -> import into the folder it is
    // currently browsing; anywhere else -> let AssetImporter classify.
    std::string dir;
    if (m_content_browser && m_content_browser->IsVisible() &&
        m_content_browser->IsPointInside(logical))
        dir = m_content_browser->CurrentDir();

    std::vector<std::string> mesh_dests;  // imported mesh paths for viewport spawn
    int imported = 0;
    std::string last_error;
    for (const std::string &path : m_pending_drops)
    {
        if (path.empty())
            continue;  // defensive: ignore malformed drop entries
        AssetImporter::Result r = dir.empty()
            ? AssetImporter::Import(path) : AssetImporter::Import(path, dir);
        if (r.ok)
        {
            ++imported;
            if (AssetImporter::ClassifyDir(path) == "meshes")
                mesh_dests.push_back(r.dest);
        }
        else
        {
            last_error = r.error;
        }
    }

    // Freshly imported assets appear in the browser without a manual refresh.
    if (m_content_browser)
    {
        m_content_browser->Refresh();
        if (imported > 0)
            m_content_browser->FlashImportResult(imported);
    }

    // Mesh files dropped over the 3D viewport also spawn as entities at the
    // cursor's ground point (editor only; undoable via PushSpawn). The
    // primary viewport region owns drops in multi-viewport layouts.
    if (m_state == EngineState::Editor && !mesh_dests.empty() && m_viewport)
    {
        ImVec2 img_min = m_viewport->GetImageMin();
        ImVec2 img_size = m_viewport->GetImageSize();
        int px, py, pw, ph;
        const bool has_primary = GetPrimaryViewportRect(px, py, pw, ph);
        if (has_primary)
        {
            // Primary region's on-screen rect (logical pixels).
            const float r_scale_x = (img_size.x > 1.0f && m_viewport_target_w > 0)
                ? img_size.x / (float)m_viewport_target_w : 1.0f;
            const float r_scale_y = (img_size.y > 1.0f && m_viewport_target_h > 0)
                ? img_size.y / (float)m_viewport_target_h : 1.0f;
            const ImVec2 r_min{ img_min.x + px * r_scale_x, img_min.y + py * r_scale_y };
            const ImVec2 r_max{ r_min.x + pw * r_scale_x, r_min.y + ph * r_scale_y };
            const bool inside = logical.x >= r_min.x && logical.x <= r_max.x &&
                                logical.y >= r_min.y && logical.y <= r_max.y;
            if (inside)
            {
                const float sx = (logical.x - img_min.x) * ((float)m_viewport_target_w / img_size.x) - (float)px;
                const float sy = (logical.y - img_min.y) * ((float)m_viewport_target_h / img_size.y) - (float)py;
                Vec3 pos;
                if (ComputeDropWorldPos(sx, sy, (float)pw, (float)ph, pos))
                    for (const std::string &dest : mesh_dests)
                        SpawnMeshEntity(dest, pos);
            }
        }
    }

    m_scene_status = imported > 0
        ? "Imported " + std::to_string(imported) + " file(s) into assets/"
        : "Import failed: " + last_error;
    if (imported > 0)
        PushToast("Imported " + std::to_string(imported) + " file(s)");
    else if (!last_error.empty())
        PushToast("Import failed: " + last_error);

    m_pending_drops.clear();
}

void Application::PushToast(const std::string &text)
{
    m_toasts.Push(text, (uint64_t)SDL_GetTicks());
}

void Application::DrawStatusBar(float dt)
{
    (void)dt;
    if (!m_status_bar_visible)
        return;

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float bar_h = kStatusBarHeight;
    ImVec2 work_pos = viewport->WorkPos;
    ImVec2 work_size = viewport->WorkSize;

    ImGui::SetNextWindowPos(ImVec2(work_pos.x, work_pos.y + work_size.y - bar_h));
    ImGui::SetNextWindowSize(ImVec2(work_size.x, bar_h));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 0.0f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##StatusBar", nullptr, flags);

    ImGui::TextColored(ImVec4(m_theme_colors.accent[0], m_theme_colors.accent[1],
                              m_theme_colors.accent[2], m_theme_colors.accent[3]),
                       "%s", WorkspaceManager::WorkspaceName(m_workspace_manager.GetWorkspace()));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_scene_status.empty() ? "Ready" : m_scene_status.c_str());

    // Right-aligned live metrics.
    const size_t entity_count = m_scene ? m_scene->GetEntities().size() : 0;
    const int audio_channels = m_audio ? m_audio->ActiveChannelCount() : 0;
    const size_t active_viewports = m_cameras ? m_cameras->EnabledCount() : 0;
    const std::string right_text =
        std::to_string((int)std::lround(m_fps)) + " FPS   |   " +
        std::to_string(active_viewports) + " viewport" +
        (active_viewports == 1 ? "" : "s") + "   |   " +
        std::to_string(audio_channels) + " audio ch   |   " +
        std::to_string(entity_count) + " entities";
    const float right_w = ImGui::CalcTextSize(right_text.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - right_w - 8.0f);
    ImGui::TextUnformatted(right_text.c_str());

    ImGui::End();
    ImGui::PopStyleVar(4);
}

void Application::DrawViewportToolbar()
{
    // The docked header bar inside the Viewport window (Phase 29). It owns no
    // state: it edits m_overlay (render modes + overlay toggles) and m_snap
    // (grid snapping), the same values the render passes and gizmo math read.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

    // Row 1: render modes (Lit / Wireframe / Unlit), then overlay toggles.
    ImGui::TextDisabled("Render:");
    ImGui::SameLine();
    for (int i = 0; i <= (int)ViewportRenderMode::Unlit; ++i)
    {
        const ViewportRenderMode mode = (ViewportRenderMode)i;
        const bool active = (m_overlay.render_mode == mode);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.38f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.45f, 1.00f));
        }
        if (ImGui::Button(ViewportOverlaySettings::RenderModeLabel(mode)))
            m_overlay.SetRenderMode(mode);
        if (active)
            ImGui::PopStyleColor(2);
        if (i < (int)ViewportRenderMode::Unlit)
            ImGui::SameLine();
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    ImGui::Checkbox("Grid", &m_overlay.grid);
    ImGui::SameLine();
    ImGui::Checkbox("Colliders", &m_overlay.colliders);
    ImGui::SameLine();
    ImGui::Checkbox("Light Gizmos", &m_overlay.light_gizmos);
    ImGui::SameLine();
    ImGui::Checkbox("Bounds", &m_overlay.bounds);
    ImGui::SameLine();
    ImGui::Checkbox("Gizmo", &m_overlay.gizmo);
    ImGui::SameLine();
    ImGui::Checkbox("HUD", &m_overlay.hud);

    // Row 2: grid-snapping quick controls. The increments drive the same
    // SnapSettings the gizmo math snaps against; full sliders stay in Editor
    // Settings.
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
        ImGui::SetTooltip("Toggle grid snapping. Hold Ctrl during a gizmo drag to snap temporarily.");
    ImGui::SameLine();
    ImGui::TextDisabled("T");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56.0f);
    ImGui::DragFloat("##vp_snap_t", &m_snap.translation, 0.05f, 0.01f, 100.0f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("R");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56.0f);
    ImGui::DragFloat("##vp_snap_r", &m_snap.rotation, 1.0f, 1.0f, 360.0f, "%.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("S");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56.0f);
    ImGui::DragFloat("##vp_snap_s", &m_snap.scale, 0.05f, 0.01f, 10.0f, "%.2f");

    ImGui::PopStyleVar(3);
}

void Application::DrawViewportHud()
{
    // On-viewport stats overlay (Phase 29): FPS, the editor camera position,
    // and the active render mode, drawn over the top-left corner of the 3D
    // image. Editor-only (the overlay callback never fires in play mode).
    if (!m_overlay.hud || !m_viewport)
        return;

    const ImVec2 img_min = m_viewport->GetImageMin();
    const ImVec2 img_size = m_viewport->GetImageSize();
    if (img_size.x < 8.0f || img_size.y < 8.0f)
        return;

    char line1[96], line2[96];
    snprintf(line1, sizeof(line1), "%s | %d FPS",
             ViewportOverlaySettings::RenderModeLabel(m_overlay.render_mode),
             (int)std::lround(m_fps));
    snprintf(line2, sizeof(line2), "Cam (%.2f, %.2f, %.2f)",
             m_editor_camera.position.x,
             m_editor_camera.position.y,
             m_editor_camera.position.z);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 pad(8.0f, 6.0f);
    const ImVec2 ts1 = ImGui::CalcTextSize(line1);
    const ImVec2 ts2 = ImGui::CalcTextSize(line2);
    const float box_w = std::max(ts1.x, ts2.x) + pad.x * 2.0f;
    const float box_h = ts1.y + ts2.y + pad.y * 2.0f + 2.0f;

    const ImVec2 p0(img_min.x + 8.0f, img_min.y + 8.0f);
    const ImVec2 p1(p0.x + box_w, p0.y + box_h);
    dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 24, 190));
    dl->AddRect(p0, p1, IM_COL32(90, 90, 110, 255), 0.0f);
    dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y),
                IM_COL32(230, 230, 240, 255), line1);
    dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y + ts1.y + 2.0f),
                IM_COL32(180, 190, 210, 255), line2);
}

void Application::DrawToasts()
{
    const uint64_t now = (uint64_t)SDL_GetTicks();
    m_toasts.Update(now);
    if (m_toasts.empty())
        return;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    float y = ImGui::GetFrameHeight() + 10.0f;  // below the main menu bar
    for (size_t i = 0; i < m_toasts.Count(); ++i)
    {
        const ToastManager::Toast *toast = m_toasts.Get(i);
        const float alpha = (i + 1 == m_toasts.Count())
            ? m_toasts.NewestFade(now) : 1.0f;
        const ImVec2 text_size = ImGui::CalcTextSize(toast->text.c_str());
        const float pad = 10.0f;
        const float w = text_size.x + pad * 2.0f;
        const float h = text_size.y + pad;

        const ImVec2 p0(display.x - w - 14.0f, y);
        const ImVec2 p1(p0.x + w, p0.y + h);
        const ImU32 bg = ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.10f, 0.92f * alpha));
        const ImU32 border = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.16f * alpha));
        dl->AddRectFilled(p0, p1, bg, 6.0f);
        dl->AddRect(p0, p1, border, 6.0f);
        dl->AddText(ImVec2(p0.x + pad, p0.y + pad * 0.5f),
                    ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)),
                    toast->text.c_str());
        y += h + 8.0f;
    }
}

void Application::DrawViewportContextMenu()
{
    if (ImGui::BeginPopup("Viewport Context"))
    {
        bool has_sel = m_selection && m_selection->entity_id >= 0;
        Entity *selected = has_sel ? m_scene->GetEntityById(m_selection->entity_id) : nullptr;
        if (selected)
        {
            if (ImGui::MenuItem("Rename..."))
            {
                m_viewport_rename_entity = selected->id;
                std::strncpy(m_viewport_rename_buffer, selected->tag.tag.c_str(),
                             sizeof(m_viewport_rename_buffer) - 1);
                m_viewport_rename_buffer[sizeof(m_viewport_rename_buffer) - 1] = '\0';
                m_viewport_rename_open = true;
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                DuplicateSelection();
            if (ImGui::MenuItem("Delete"))
                DeleteSelection();
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Create Empty Entity"))
            SpawnPrimitive("New Entity", nullptr, nullptr);
        if (ImGui::MenuItem("Create Cube"))
            SpawnPrimitive("Cube", nullptr, "Checker.mat");
        if (ImGui::MenuItem("Create Octahedron"))
            SpawnPrimitive("Octahedron", "octahedron.obj", nullptr);
        if (ImGui::MenuItem("Create Landscape"))
        {
            // Spawn + select + arm the brush, then jump into the Landscape
            // workspace so the viewport override is immediately active.
            if (CreateLandscape())
            {
                m_script_editor->RequestDockCodeWindow(
                    ApplyWorkspace(WorkspaceManager::Workspace::Landscape));
            }
        }
        if (ImGui::MenuItem("Create Directional Light"))
        {
            Entity &light = CreateDirectionalLightEntity(*m_scene, "Directional Light");
            if (m_history)
                m_history->PushSpawn(light, "Create 'Directional Light'");
            m_selection->entity_id = light.id;
            m_selection->entity_name = light.tag.tag;
            m_scene_status = "Created 'Directional Light'";
            PushToast("Created 'Directional Light'");
        }
        if (ImGui::MenuItem("Create Camera"))
        {
            Entity &cam = m_scene->CreateEntity("Camera");
            cam.transform.position[0] = m_editor_camera.position.x;
            cam.transform.position[1] = m_editor_camera.position.y;
            cam.transform.position[2] = m_editor_camera.position.z;
            cam.camera.pitch = m_editor_camera.pitch;
            cam.camera.yaw = m_editor_camera.yaw;
            if (m_history)
                m_history->PushSpawn(cam, "Create 'Camera'");
            m_selection->entity_id = cam.id;
            m_selection->entity_name = cam.tag.tag;
            m_scene_status = "Created 'Camera'";
            PushToast("Created 'Camera'");
        }
        ImGui::EndPopup();
    }

    // "Rename..." over the viewport opens a small modal (the Hierarchy uses an
    // inline row; the viewport has no inline surface, so a modal is the
    // harmonized equivalent).
    if (m_viewport_rename_open)
    {
        ImGui::OpenPopup("Rename Entity");
        m_viewport_rename_open = false;
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        Entity *target = m_scene->GetEntityById(m_viewport_rename_entity);
        if (!target)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::TextUnformatted("Rename entity:");
        ImGui::SetNextItemWidth(300.0f);
        const bool committed = ImGui::InputText(
            "##name", m_viewport_rename_buffer, sizeof(m_viewport_rename_buffer),
            ImGuiInputTextFlags_EnterReturnsTrue);
        const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (committed && !cancelled && m_viewport_rename_buffer[0] != '\0')
        {
            const std::string new_name = m_viewport_rename_buffer;
            if (m_history)
                m_history->BeginEntityEdit(target->id, "Rename");
            target->tag.tag = new_name;
            if (m_history)
                m_history->EndEntityEdit();
            if (m_selection->entity_id == target->id)
                m_selection->entity_name = new_name;
            m_scene_status = "Renamed to '" + new_name + "'";
            PushToast("Renamed to '" + new_name + "'");
            ImGui::CloseCurrentPopup();
        }
        else if (committed || cancelled)
        {
            ImGui::CloseCurrentPopup();
        }
        if (!committed)
        {
            if (ImGui::Button("OK"))
            {
                if (m_viewport_rename_buffer[0] != '\0')
                {
                    if (m_history)
                        m_history->BeginEntityEdit(target->id, "Rename");
                    target->tag.tag = m_viewport_rename_buffer;
                    if (m_history)
                        m_history->EndEntityEdit();
                    if (m_selection->entity_id == target->id)
                        m_selection->entity_name = m_viewport_rename_buffer;
                    m_scene_status = "Renamed to '" + std::string(m_viewport_rename_buffer) + "'";
                    PushToast("Renamed to '" + std::string(m_viewport_rename_buffer) + "'");
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::UpdateCameraControls(float dt)
{
    if (!m_scene || !m_viewport)
        return;

    // Advance any in-flight Play/Stop camera blend. The viewport keeps
    // following it (GetActiveCameraPose) until the blend reaches its target.
    UpdateCameraTransition(dt);

    // Fly Mode is an editor feature: the isolated game viewport never captures
    // the cursor, so the Stop button stays clickable during play.
    if (m_state != EngineState::Editor)
        return;

    // Consume the accumulators every frame so no stale motion leaks into a
    // later navigation session (drag deltas, scroll deltas).
    int rel_x = 0, rel_y = 0;
    SDL_GetRelativeMouseState(&rel_x, &rel_y);
    const float scroll = m_camera_scroll;
    m_camera_scroll = 0.0f;

    const bool over_viewport = m_viewport->IsHovered();
    const bool rmb_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool rmb_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const float kFlyThresholdPx = 3.0f;

    // Right-click over the viewport is a two-way gesture: a quick click opens
    // the viewport context menu; press-and-move (past the threshold) captures
    // the cursor and enters Fly Mode. Skipped while a camera blend is still in
    // flight so the blend isn't hijacked.
    if (rmb_clicked && over_viewport &&
        m_camera_transition.phase == CameraTransitionPhase::None)
    {
        m_viewport_rmb_pending = true;
        const ImVec2 mouse = ImGui::GetMousePos();
        m_viewport_rmb_down_x = mouse.x;
        m_viewport_rmb_down_y = mouse.y;
    }

    if (m_viewport_rmb_pending && !rmb_down)
    {
        // Released without a fly-drag: treat it as a context-menu click.
        m_viewport_rmb_pending = false;
        if (over_viewport)
            ImGui::OpenPopup("Viewport Context");
    }

    if (m_viewport_rmb_pending && rmb_down)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float dx = mouse.x - m_viewport_rmb_down_x;
        const float dy = mouse.y - m_viewport_rmb_down_y;
        if (dx * dx + dy * dy > kFlyThresholdPx * kFlyThresholdPx)
        {
            m_viewport_rmb_pending = false;
            if (m_camera_transition.phase == CameraTransitionPhase::None)
            {
                // The OS cursor is captured (relative mode) for unlimited
                // rotation and ImGui is told to ignore the mouse so the hidden
                // cursor never triggers a panel.
                m_flying = true;
                SDL_SetRelativeMouseMode(SDL_TRUE);
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                SDL_GetRelativeMouseState(nullptr, nullptr); // drain pre-capture motion
            }
        }
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
        // Hover-only action: scroll wheel zoom of the editor camera.
        if (over_viewport && scroll != 0.0f)
        {
            m_editor_camera.fov *= std::pow(0.9f, scroll);
            if (m_editor_camera.fov < 10.0f)  m_editor_camera.fov = 10.0f;
            if (m_editor_camera.fov > 120.0f) m_editor_camera.fov = 120.0f;
        }
        return;
    }

    const float pitch = m_editor_camera.pitch;
    const float yaw   = m_editor_camera.yaw;

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
    const float move_speed = m_camera_settings.fly_speed; // world-units per second
    float mv_f = 0.0f, mv_r = 0.0f, mv_u = 0.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    mv_f += 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  mv_f -= 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  mv_r -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) mv_r += 1.0f;
    if (keys[SDL_SCANCODE_E]) mv_u += 1.0f;
    if (keys[SDL_SCANCODE_Q]) mv_u -= 1.0f;

    if (mv_f != 0.0f || mv_r != 0.0f || mv_u != 0.0f)
    {
        m_editor_camera.position.x += (fx * mv_f + rx * mv_r) * move_speed * dt;
        m_editor_camera.position.y += mv_u * move_speed * dt;
        m_editor_camera.position.z += (fz * mv_f + rz * mv_r) * move_speed * dt;
    }

    // --- Mouse look (pitch/yaw), FPS-style ---
    m_editor_camera.yaw   -= rel_x * m_camera_settings.rotation_sensitivity;
    m_editor_camera.pitch -= rel_y * m_camera_settings.rotation_sensitivity;
    if (m_editor_camera.pitch >  89.0f) m_editor_camera.pitch =  89.0f;
    if (m_editor_camera.pitch < -89.0f) m_editor_camera.pitch = -89.0f;

    // --- Scroll wheel zoom (fov = vertical field of view in degrees) ---
    if (scroll != 0.0f)
    {
        m_editor_camera.fov *= std::pow(0.9f, scroll);
        if (m_editor_camera.fov < 10.0f)  m_editor_camera.fov = 10.0f;
        if (m_editor_camera.fov > 120.0f) m_editor_camera.fov = 120.0f;
    }
}

bool Application::Init(int width, int height, const char *title)
{
    // Redirect C stdout/stderr into the engine console before anything else
    // can write to a terminal: printf/std::cout/Lua-stdlib output now lands in
    // the ConsolePanel, so no external console window is needed. Failing the
    // redirect is non-fatal (direct Write() calls still work).
    Console::Instance().StartRedirect();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
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

    // Reserve the bottom strip of the dock host for the status bar so docked
    // panels never draw underneath it.
    m_workspace_manager.SetBottomBarHeight(kStatusBarHeight);

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

    // Phase 26 audio bridge: the mixer opens the device up front (reporting a
    // console error instead of crashing on machines without audio hardware)
    // and the script session routes its Audio.* bindings through it.
    m_audio = new AudioManager();
    m_audio->Init();
    m_script_engine->SetAudioManager(m_audio);

    // Phase 27 camera stack: a pure, headless-testable list of camera entries
    // (source + normalized viewport rect + z-order). It ships with the classic
    // single full-screen viewport; the Viewport Layout panel edits it live.
    m_cameras = new CameraManager();
    m_cameras->ResetToSingleViewport();

    // The SceneManager owns the active Scene. The object is allocated once and
    // rebuilt in place on every load, so the Scene* kept by every panel stays
    // valid across scene switches.
    m_scene_manager = new SceneManager();
    m_scene = m_scene_manager->GetScene();

    // Global undo/redo history (Phase 22): CommandHistory owns the undo and
    // redo stacks; every editor action (gizmo drags, inspector edits, entity
    // deletes) routes through it, and the History panel renders its contents.
    m_history = new CommandHistory(m_scene);

    // Gizmo drags are undo transactions: BeginEntityEdit snapshots the entity
    // when the drag starts, EndEntityEdit compares it with the final transform
    // and pushes a single command when something actually changed.
    m_gizmo->on_drag_start = [this](int entity_id) {
        const char *desc = "Move";
        if (m_gizmo->mode == GizmoMode::Rotate)
            desc = "Rotate";
        else if (m_gizmo->mode == GizmoMode::Scale)
            desc = "Scale";
        m_history->BeginEntityEdit(entity_id, desc);
    };
    m_gizmo->on_drag_end = [this]() {
        m_history->EndEntityEdit();
    };

    Entity &camera = m_scene->CreateEntity("Camera");
    camera.transform.position[1] = 2.0f;
    camera.transform.position[2] = 8.0f;
    camera.camera.pitch = -14.0f;
    CreateDirectionalLightEntity(*m_scene, "Directional Light");
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
    // Phase 26 demo: the Bouncer beeps once on play start (component auto_play)
    // and its script triggers another beep on collision via Audio.Play().
    bouncer.audio.path = "assets/audio/beep.wav";
    bouncer.audio.volume = 0.8f;
    bouncer.audio.auto_play = true;

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

    // Phase 29 viewport chrome: the header toolbar and stats HUD are drawn by
    // the Application inside the Viewport window through these callbacks (the
    // same on_drop wiring pattern). The toolbar edits m_overlay/m_snap, so it
    // stays the single source of truth for the render passes and gizmo math.
    m_viewport->on_toolbar = [this]() { DrawViewportToolbar(); };
    m_viewport->on_overlay = [this]() { DrawViewportHud(); };

    // Viewport drag-drop (Phase 23): a prefab/mesh dropped onto the 3D view
    // spawns an instance at the cursor's ground point; a material/texture is
    // assigned to the current selection. Editor-only.
    m_viewport->on_drop = [this](const char *type, const char *payload) {
        // Robust drop dispatch: every payload (including hostile/empty drags)
        // must be rejected before any scene mutation. `type` is one of the
        // ViewportPanel constants, but never index past its end.
        if (!payload || !*payload || m_state != EngineState::Editor || !m_scene ||
            !m_selection || !type || !*type)
            return;

        const size_t tlen = std::strlen(type);
        const bool is_mesh = tlen >= 2 && type[0] == 'M' && type[1] == 'E';
        const bool is_prefab = type[0] == 'P';
        const bool is_material = tlen >= 2 && type[0] == 'M' && type[1] == 'A';

        if (is_mesh || is_prefab)
        {
            Vec3 pos{0.0f, 0.0f, 0.0f};
            ComputeDropWorldPosFromMouse(pos);
            if (is_mesh)
            {
                SpawnMeshEntity(payload, pos);
            }
            else
            {
                std::string error;
                if (Entity *spawned = SceneSerializer::LoadPrefab(*m_scene, payload, nullptr, &error))
                {
                    spawned->transform.position[0] = pos.x;
                    spawned->transform.position[1] = pos.y;
                    spawned->transform.position[2] = pos.z;
                    if (m_history)
                        m_history->PushSpawn(*spawned,
                            ("Spawn prefab '" + std::string(payload) + "'").c_str());
                    m_selection->entity_id = spawned->id;
                    m_selection->entity_name = spawned->tag.tag;
                    m_scene_status = "Spawned prefab '" + std::string(payload) +
                                     "' at drop position";
                }
                else
                {
                    m_scene_status = "Prefab spawn failed: " + error;
                }
            }
        }
        else if (m_selection && m_selection->entity_id >= 0)
        {
            if (Entity *selected = m_scene->GetEntityById(m_selection->entity_id))
            {
                if (m_history)
                    m_history->BeginEntityEdit(selected->id,
                        is_material ? "Assign Material" : "Assign Texture");
                if (is_material)
                    selected->material.material_path = payload;
                else
                    selected->material.texture_path = payload;
                if (m_history)
                    m_history->EndEntityEdit();
                m_scene_status = std::string(is_material ? "Assigned material " : "Assigned texture ") +
                                 payload + " to '" + selected->tag.tag + "'";
            }
        }
    };

    m_panels.push_back(
        std::make_shared<StatsPanel>(m_window)
    );
    m_panels.push_back(
        std::make_shared<SceneHierarchyPanel>(m_selection, m_scene, m_history)
    );

    // Phase 35 animation & timeline foundation: the shared bridge routes the
    // Timeline panel's transport/lanes and the Inspector's keyframe toggles
    // into Application-owned undoable actions. The bridge is a member, so its
    // callbacks only need to capture `this`.
    m_timeline_bridge.state = &m_timeline;
    m_timeline_bridge.on_play_pause = [this]() { PlayPauseTimeline(); };
    m_timeline_bridge.on_stop = [this]() { StopTimeline(); };
    m_timeline_bridge.on_scrub = [this]() { ScrubTimeline(); };
    m_timeline_bridge.on_set_keyframe = [this](AnimProperty prop) {
        SetTimelineKeyframe(prop);
    };
    m_timeline_bridge.on_remove_keyframe = [this](AnimProperty prop, float time) {
        RemoveTimelineKeyframe(prop, time);
    };

    m_inspector_panel = new InspectorPanel(m_selection, m_scene,
                                           m_material_library, m_texture_library,
                                           m_history, m_audio,
                                           [this](int w, int h) -> void * {
        RecreateCameraPreview(w, h);
        return (void *)m_camera_preview;
    }, &m_timeline_bridge);
    m_panels.push_back(std::shared_ptr<InspectorPanel>(m_inspector_panel));
    m_panels.push_back(std::shared_ptr<ViewportPanel>(m_viewport));

    // Phase 27 viewport layout editor: edits the CameraManager camera stack
    // (sources, normalized rects, z-order, primary). Restoring a layout is
    // undoable; direct structural edits apply immediately.
    m_viewport_layout_panel = new ViewportLayoutPanel(m_cameras);
    m_panels.push_back(std::shared_ptr<ViewportLayoutPanel>(m_viewport_layout_panel));

    // Script editor: a tabbed mini-IDE (browser sidebar + multiple script
    // tabs) docked by name into the workspace layouts. Its reload callback
    // hot-swaps the running play session after a save so script edits apply
    // without leaving play mode; its redock callback pops the IDE back into
    // the current workspace when the user re-docks it from floating state.
    m_script_editor = new ScriptEditorPanel(m_fonts.mono,
        [this]() -> bool {
            if (m_state != EngineState::Play)
                return false;
            std::string script_errors;
            m_script_engine->ReloadSession(*m_scene, script_errors);
            m_scene_status = script_errors.empty()
                                 ? "Script session reloaded (OnStart re-ran)"
                                 : "Reload errors -> " + script_errors;
            return true;
        },
        [this]() {
            // Re-apply the active workspace: rebuilds its canonical layout and
            // routes the mini-IDE window back into its dedicated dock node.
            m_script_editor->RequestDockCodeWindow(
                ApplyWorkspace(m_workspace_manager.GetWorkspace()));
        });
    m_panels.push_back(std::shared_ptr<ScriptEditorPanel>(m_script_editor));

    // Phase 34 landscape & topology design: the terrain sculpting panel owns no
    // scene state — it edits the shared LandscapeBrushSettings and routes
    // terrain creation through the Application (spawn + undo + selection).
    m_landscape_panel = new LandscapePanel(m_scene, m_selection, &m_landscape_brush,
        [this]() { CreateLandscape(); });
    m_panels.push_back(std::shared_ptr<LandscapePanel>(m_landscape_panel));

    // Phase 35: the track-based timeline editor. Reads the shared clock and
    // fires transport / keyframe actions back through the bridge.
    m_timeline_panel = new TimelinePanel(m_scene, m_selection, &m_timeline_bridge);
    m_panels.push_back(std::shared_ptr<TimelinePanel>(m_timeline_panel));

    // The sequencing workspace hides the viewport; apply the side effects of
    // whatever workspace the persisted layout restored.
    SyncWorkspaceSideEffects(m_workspace_manager.GetWorkspace());

    // Live theme customizer + grid snapping config + viewport navigation
    // tuning: owns no state itself — it edits Application's token set (and
    // SnapSettings / EditorCameraSettings) and asks for a ConfigureStyle
    // re-apply on every theme change.
    m_settings_panel = new SettingsPanel(&m_theme_colors, &m_snap, &m_camera_settings,
                                         [this]() {
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
    cp.Register({ "Command Palette", "View", "Ctrl+P", [this]() {
        if (m_command_palette)
            m_command_palette->ToggleOpen();
    } });
    cp.Register({ "Reset UI Scale", "View", "", [this]() {
        m_ui_scale = 1.0f;
    } });
    cp.Register({ "Switch to Level Design Workspace", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            ApplyWorkspace(WorkspaceManager::Workspace::LevelDesign));
    } });
    cp.Register({ "Switch to Scripting Workspace", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            ApplyWorkspace(WorkspaceManager::Workspace::Scripting));
    } });
    cp.Register({ "Switch to Shading & Assets Workspace", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            ApplyWorkspace(WorkspaceManager::Workspace::ShadingAndAssets));
    } });
    cp.Register({ "Switch to Landscape Mode", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            ApplyWorkspace(WorkspaceManager::Workspace::Landscape));
    } });
    cp.Register({ "Switch to Sequencing Workspace", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(
            ApplyWorkspace(WorkspaceManager::Workspace::Timeline));
    } });
    cp.Register({ "Reset View to Workspace Default", "Workspace", "", [this]() {
        m_script_editor->RequestDockCodeWindow(ResetWorkspaceDefault());
        PushToast("Reset to workspace default");
    } });
    cp.Register({ "Save Current Layout as Default", "Workspace", "", [this]() {
        m_workspace_manager.RequestSaveCurrent();
    } });
    cp.Register({ "Save Scene", "File", "Ctrl+S", [this]() { SaveScene(); } });
    cp.Register({ "Open Scene", "File", "Ctrl+O", [this]() { OpenScene(); } });
    cp.Register({ "New Scene", "File", "", [this]() { NewScene(); } });
    cp.Register({ "Save Scene As...", "File", "", [this]() { OpenSaveAsModal(); } });
    cp.Register({ "Enter Play Mode", "Transport", "", [this]() { EnterPlayMode(); } });
    cp.Register({ "Stop Play Mode", "Transport", "", [this]() { ExitPlayMode(); } });

    // Entity operations (palette + viewport context menu share these paths).
    cp.Register({ "Duplicate Selected", "Edit", "Ctrl+D", [this]() { DuplicateSelection(); } });
    cp.Register({ "Delete Selected", "Edit", "", [this]() { DeleteSelection(); } });
    cp.Register({ "Create Empty Entity", "Create", "", [this]() {
        SpawnPrimitive("New Entity", nullptr, nullptr);
    } });
    cp.Register({ "Create Cube", "Create", "", [this]() {
        SpawnPrimitive("Cube", nullptr, "Checker.mat");
    } });
    cp.Register({ "Create Landscape", "Create", "", [this]() {
        // Spawn + select + arm the brush, then jump into the Landscape
        // workspace so the viewport override is immediately active.
        if (CreateLandscape())
        {
            m_script_editor->RequestDockCodeWindow(
                ApplyWorkspace(WorkspaceManager::Workspace::Landscape));
        }
    } });
    cp.Register({ "Create Octahedron", "Create", "", [this]() {
        SpawnPrimitive("Octahedron", "octahedron.obj", nullptr);
    } });
    cp.Register({ "Create Directional Light", "Create", "", [this]() {
        Entity &light = CreateDirectionalLightEntity(*m_scene, "Directional Light");
        if (m_history)
            m_history->PushSpawn(light, "Create 'Directional Light'");
        m_selection->entity_id = light.id;
        m_selection->entity_name = light.tag.tag;
        m_scene_status = "Created 'Directional Light'";
        PushToast("Created 'Directional Light'");
    } });
    cp.Register({ "Create Camera", "Create", "", [this]() {
        Entity &cam = m_scene->CreateEntity("Camera");
        cam.transform.position[0] = m_editor_camera.position.x;
        cam.transform.position[1] = m_editor_camera.position.y;
        cam.transform.position[2] = m_editor_camera.position.z;
        cam.camera.pitch = m_editor_camera.pitch;
        cam.camera.yaw = m_editor_camera.yaw;
        if (m_history)
            m_history->PushSpawn(cam, "Create 'Camera'");
        m_selection->entity_id = cam.id;
        m_selection->entity_name = cam.tag.tag;
        m_scene_status = "Created 'Camera'";
        PushToast("Created 'Camera'");
    } });

    // Content Browser: dockable asset manager over assets/ (scene/prefab
    // load & spawn, script open, folder/file ops). Its window is docked by
    // name in both workspace presets; the command toggles visibility so the
    // docked slot can be dismissed and restored.
    m_content_browser = new ContentBrowserPanel(m_scene_manager, m_script_editor,
                                                m_material_library, m_texture_library,
                                                m_window->GetNativeRenderer(), m_mesh_library);
    m_content_browser->on_load_scene = [this](const std::string &path) { LoadSceneFile(path); };
    m_panels.push_back(std::shared_ptr<ContentBrowserPanel>(m_content_browser));
    cp.Register({ "Toggle Content Browser", "View", "", [this]() {
        if (m_content_browser)
            m_content_browser->ToggleVisible();
    } });

    // Console: dockable log window fed by the shared Console sink (Lua print,
    // script exceptions, engine messages, and redirected stdout/stderr).
    m_console_panel = new ConsolePanel();
    // The console's Lua REPL line executes snippets against the active scene
    // (edit or play) through the persistent ScriptEngine::Execute state.
    m_console_panel->on_execute = [this](const std::string &code) {
        if (!m_script_engine)
            return;
        std::string error;
        m_script_engine->Execute(*m_scene, code, error);
        if (!error.empty())
            m_scene_status = "REPL: " + error;
    };
    m_panels.push_back(std::shared_ptr<ConsolePanel>(m_console_panel));
    cp.Register({ "Toggle Console", "View", "", [this]() {
        if (m_console_panel)
            m_console_panel->ToggleVisible();
    } });

    // Material Editor: dedicated dockable authoring panel over assets/materials/
    // (diffuse tint, texture slot + live preview, shininess, create/save). The
    // primary zone of the Shading & Assets workspace; a tab elsewhere.
    m_material_panel = new MaterialPanel(m_material_library, m_texture_library);
    m_panels.push_back(std::shared_ptr<MaterialPanel>(m_material_panel));
    cp.Register({ "Toggle Material Editor", "View", "", [this]() {
        if (m_material_panel)
            m_material_panel->ToggleVisible();
    } });

    // History panel (Phase 22): read-only undo/redo stacks with Undo/Redo/
    // Clear buttons. A dockable tab in the dev zone; the palette command and
    // the Edit menu toggle it.
    m_history_panel = new HistoryPanel(m_history);
    m_panels.push_back(std::shared_ptr<HistoryPanel>(m_history_panel));
    cp.Register({ "Toggle History Panel", "View", "", [this]() {
        if (m_history_panel)
            m_history_panel->ToggleVisible();
    } });

    // Profiler panel (Phase 30): live performance telemetry (per-stage frame
    // times, entity count, draw calls, resident memory) with a Pause/Freeze
    // snapshot and Clear. Reads the Application's Profiler instance.
    m_profiler_panel = new ProfilerPanel(&m_profiler);
    m_panels.push_back(std::shared_ptr<ProfilerPanel>(m_profiler_panel));
    cp.Register({ "Toggle Profiler", "View", "", [this]() {
        if (m_profiler_panel)
            m_profiler_panel->ToggleVisible();
    } });
    cp.Register({ "Pause/Resume Profiler", "View", "", [this]() {
        m_profiler.SetPaused(!m_profiler.IsPaused());
    } });
    cp.Register({ "Clear Profiler Data", "View", "", [this]() {
        m_profiler.Clear();
    } });

    cp.Register({ "Toggle Viewport Layout", "View", "", [this]() {
        if (m_viewport_layout_panel)
            m_viewport_layout_panel->ToggleVisible();
    } });
    cp.Register({ "Reset Viewport Layout", "View", "", [this]() {
        if (m_cameras)
            m_cameras->ResetToSingleViewport();
    } });

    // Phase 29 viewport overlays & render modes (mirrors the header toolbar
    // and the View menu; all edit the shared ViewportOverlaySettings).
    cp.Register({ "Set Render Mode: Lit", "View", "", [this]() {
        m_overlay.SetRenderMode(ViewportRenderMode::Lit);
    } });
    cp.Register({ "Set Render Mode: Wireframe", "View", "", [this]() {
        m_overlay.SetRenderMode(ViewportRenderMode::Wireframe);
    } });
    cp.Register({ "Set Render Mode: Unlit", "View", "", [this]() {
        m_overlay.SetRenderMode(ViewportRenderMode::Unlit);
    } });
    cp.Register({ "Toggle Grid", "View", "", [this]() {
        m_overlay.grid = !m_overlay.grid;
    } });
    cp.Register({ "Toggle Colliders", "View", "", [this]() {
        m_overlay.colliders = !m_overlay.colliders;
    } });
    cp.Register({ "Toggle Light Gizmos", "View", "", [this]() {
        m_overlay.light_gizmos = !m_overlay.light_gizmos;
    } });
    cp.Register({ "Toggle Bounding Boxes", "View", "", [this]() {
        m_overlay.bounds = !m_overlay.bounds;
    } });
    cp.Register({ "Toggle Transform Gizmo", "View", "", [this]() {
        m_overlay.gizmo = !m_overlay.gizmo;
    } });
    cp.Register({ "Toggle Viewport HUD", "View", "", [this]() {
        m_overlay.hud = !m_overlay.hud;
    } });
    cp.Register({ "Undo", "Edit", "Ctrl+Z", [this]() {
        if (m_history) m_history->Undo();
    } });
    cp.Register({ "Redo", "Edit", "Ctrl+Y", [this]() {
        if (m_history) m_history->Redo();
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

        // Open the profiler frame and clear this frame's draw-call tally before
        // any of the stages below accumulate into them.
        m_profiler.StartFrame();
        m_draw_calls = 0;

        // Smoothed FPS for the status bar: exponential moving average so the
        // instant frame-to-frame jitter averages out.
        if (dt > 0.0)
        {
            const float inst_fps = (float)(1.0 / dt);
            m_fps = (m_fps <= 0.0f) ? inst_fps : m_fps * 0.92f + inst_fps * 0.08f;
        }

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
            if (event.type == SDL_DROPFILE)
            {
                // OS file drop (Phase 23): queue the path and the window-relative
                // mouse position; the batch is routed into the assets tree after
                // the ImGui frame has drawn (and meshes dropped over the viewport
                // spawn as entities at the cursor). The path string is owned by
                // SDL and must be freed here.
                if (event.drop.file)
                {
                    m_pending_drops.emplace_back(event.drop.file);
                    SDL_free(event.drop.file);
                }
            }
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

        // Profiler: the UI stage spans ImGui's NewFrame -> Render (the editor
        // chrome, panels, menus) and the final SDL blit + Present below; the
        // two spans never overlap the Update/Physics/Render stages.
        m_profiler.BeginStage(Profiler::UI);

        // Pull any redirected stdout/stderr bytes into the console every frame
        // (cheap: a single pipe peek when empty). The ConsolePanel also drains,
        // but doing it here keeps piped output flowing even while it is hidden.
        Console::Instance().DrainPipes();

        // Global command palette: Ctrl+P (or F1, the editor convention) toggles
        // it in editor mode; Ctrl+Shift+P is kept as a secondary chord.
        // IsKeyChordPressed reports the chord once per frame; the palette's
        // own OnImGuiRender draws the modal and handles navigation.
        if (m_state == EngineState::Editor && m_command_palette &&
            (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P) ||
             ImGui::IsKeyPressed(ImGuiKey_F1) ||
             ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P)))
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

        // Global undo/redo (Phase 22): Ctrl+Z pops the undo stack, Ctrl+Y
        // (and Ctrl+Shift+Z) re-applies a popped command. Skipped while a text
        // field is focused so typing "z" in an input never fires history, and
        // while a gizmo drag is in flight (Ctrl also means hold-to-snap) so a
        // snap-drag can't mid-cancel into an undo.
        if (m_state == EngineState::Editor &&
            m_gizmo && !m_gizmo->IsDragging() &&
            !ImGui::GetIO().WantTextInput &&
            m_history)
        {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
                m_history->Undo();
            else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                     ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
                m_history->Redo();
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
                // WorkspaceManager, which persists the active workspace. The
                // menu is grouped by section — presets first, then layout
                // utilities — and the active workspace carries a checkmark.
                if (ImGui::BeginMenu("Workspace"))
                {
                    const WorkspaceManager::Workspace current = m_workspace_manager.GetWorkspace();
                    ImGui::TextDisabled("Workspace Presets");
                    ImGui::Separator();
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::LevelDesign),
                            nullptr, current == WorkspaceManager::Workspace::LevelDesign))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            ApplyWorkspace(WorkspaceManager::Workspace::LevelDesign));
                    }
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::Scripting),
                            nullptr, current == WorkspaceManager::Workspace::Scripting))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            ApplyWorkspace(WorkspaceManager::Workspace::Scripting));
                    }
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::ShadingAndAssets),
                            nullptr, current == WorkspaceManager::Workspace::ShadingAndAssets))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            ApplyWorkspace(WorkspaceManager::Workspace::ShadingAndAssets));
                    }
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::Landscape),
                            nullptr, current == WorkspaceManager::Workspace::Landscape))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            ApplyWorkspace(WorkspaceManager::Workspace::Landscape));
                    }
                    if (ImGui::MenuItem(
                            WorkspaceManager::WorkspaceName(WorkspaceManager::Workspace::Timeline),
                            nullptr, current == WorkspaceManager::Workspace::Timeline))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            ApplyWorkspace(WorkspaceManager::Workspace::Timeline));
                    }
                    ImGui::Spacing();
                    ImGui::TextDisabled("Layout");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset to Workspace Default"))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            ResetWorkspaceDefault());
                    }
                    if (ImGui::MenuItem("Save Current Layout as Default", nullptr,
                                        m_workspace_manager.HasSavedLayout()))
                        m_workspace_manager.RequestSaveCurrent();
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Edit"))
                {
                    ImGui::BeginDisabled(!(m_history && m_history->CanUndo()));
                    if (ImGui::MenuItem("Undo", "Ctrl+Z"))
                        m_history->Undo();
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!(m_history && m_history->CanRedo()));
                    if (ImGui::MenuItem("Redo", "Ctrl+Y"))
                        m_history->Redo();
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    ImGui::BeginDisabled(!(m_selection && m_selection->entity_id >= 0));
                    if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D"))
                        DuplicateSelection();
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Undo History"))
                    {
                        if (m_history)
                            m_history->Clear();
                    }
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

                    if (m_material_panel)
                    {
                        if (ImGui::MenuItem("Material Editor", nullptr,
                                            m_material_panel->IsVisible()))
                            m_material_panel->ToggleVisible();
                    }

                    if (m_history_panel)
                    {
                        if (ImGui::MenuItem("History", nullptr,
                                            m_history_panel->IsVisible()))
                            m_history_panel->ToggleVisible();
                    }

                    if (m_profiler_panel)
                    {
                        if (ImGui::MenuItem("Profiler", nullptr,
                                            m_profiler_panel->IsVisible()))
                            m_profiler_panel->ToggleVisible();
                    }

                    if (m_viewport_layout_panel)
                    {
                        if (ImGui::MenuItem("Viewport Layout", nullptr,
                                            m_viewport_layout_panel->IsVisible()))
                            m_viewport_layout_panel->ToggleVisible();
                    }

                    if (m_command_palette)
                    {
                        if (ImGui::MenuItem("Command Palette", "Ctrl+P"))
                            m_command_palette->ToggleOpen();
                    }

                    ImGui::Separator();
                    if (ImGui::BeginMenu("Render Mode"))
                    {
                        for (int i = 0; i <= (int)ViewportRenderMode::Unlit; ++i)
                        {
                            const ViewportRenderMode mode = (ViewportRenderMode)i;
                            if (ImGui::MenuItem(
                                    ViewportOverlaySettings::RenderModeLabel(mode),
                                    nullptr, m_overlay.render_mode == mode))
                                m_overlay.SetRenderMode(mode);
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Show Grid", nullptr, m_overlay.grid))
                        m_overlay.grid = !m_overlay.grid;
                    if (ImGui::MenuItem("Show Colliders", nullptr, m_overlay.colliders))
                        m_overlay.colliders = !m_overlay.colliders;
                    if (ImGui::MenuItem("Show Light Gizmos", nullptr, m_overlay.light_gizmos))
                        m_overlay.light_gizmos = !m_overlay.light_gizmos;
                    if (ImGui::MenuItem("Show Bounding Boxes", nullptr, m_overlay.bounds))
                        m_overlay.bounds = !m_overlay.bounds;
                    if (ImGui::MenuItem("Show Transform Gizmo", nullptr, m_overlay.gizmo))
                        m_overlay.gizmo = !m_overlay.gizmo;
                    if (ImGui::MenuItem("Viewport HUD", nullptr, m_overlay.hud))
                        m_overlay.hud = !m_overlay.hud;

                    ImGui::Separator();
                    if (ImGui::MenuItem("Status Bar", nullptr, m_status_bar_visible))
                    {
                        m_status_bar_visible = !m_status_bar_visible;
                        // The dock host shrinks/grows to the reserved strip next
                        // frame; docked nodes follow the host automatically, so
                        // the user's arrangement (canonical or captured) is kept.
                        m_workspace_manager.SetBottomBarHeight(
                            m_status_bar_visible ? kStatusBarHeight : 0.0f);
                    }
                    if (ImGui::MenuItem("Reset to Workspace Default"))
                    {
                        m_script_editor->RequestDockCodeWindow(
                            ResetWorkspaceDefault());
                        PushToast("Reset to workspace default");
                    }
                    if (ImGui::MenuItem("Save Current Layout as Default"))
                        m_workspace_manager.RequestSaveCurrent();
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
            m_viewport->SetVisible(true);
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
            DrawViewportContextMenu();
            DrawStatusBar((float)dt);
            DrawToasts();
        }

        // Route OS file drops into the assets tree now that every panel drew
        // itself (so window/rect queries like the Content Browser's and the
        // viewport's are current for this frame).
        ProcessExternalDrops();

        ImGui::Render();
        m_profiler.EndStage(Profiler::UI);

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

        // Profiler: the Update stage wraps gameplay scripts and editor
        // interaction (viewport picking + gizmo drags). The physics step runs
        // nested inside it — a dedicated stage so its cost is visible, but it
        // only ever runs in play mode where the editor block below is skipped.
        m_profiler.BeginStage(Profiler::Update);
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
            {
                m_profiler.BeginStage(Profiler::Physics);
                m_physics->Step(*m_scene, *m_script_engine);
                m_profiler.EndStage(Profiler::Physics);
            }
        }

        // Phase 35 timeline playback: advances the global clock and writes the
        // sampled pose onto every animated entity. Editor-only (the play
        // session drives gameplay scripts instead) and gated on the dirty flag
        // so a paused timeline never stomps gizmo / Inspector edits.
        ApplyTimeline((float)dt);

        // Editor interaction: viewport picking + gizmo dragging. Skipped in
        // play mode, while the RMB fly camera is active (cursor captured),
        // while the timeline is playing, or when the viewport is hidden by the
        // Sequencing workspace.
        if (m_state == EngineState::Editor && !m_flying && m_gizmo && m_viewport &&
            !m_timeline.playing && m_viewport->IsVisible())
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

                // Interaction is scoped to the primary viewport region: gizmo
                // ray hits and drop targets resolve against the primary entry's
                // rect instead of the whole target, so multi-viewport layouts
                // pick consistently in whichever region owns the mouse.
                int vp_px, vp_py, vp_pw, vp_ph;
                GetPrimaryViewportRect(vp_px, vp_py, vp_pw, vp_ph);
                gf.vp_width = (float)vp_pw;
                gf.vp_height = (float)vp_ph;
                gf.dpi_scale = dpi * kViewportSupersample;
                gf.hovered = m_viewport->IsHovered();
                ImVec2 img_min = m_viewport->GetImageMin();
                ImVec2 img_size = m_viewport->GetImageSize();
                ImVec2 mouse = ImGui::GetMousePos();
                if (img_size.x > 1.0f && img_size.y > 1.0f &&
                    m_viewport_target_w > 0 && m_viewport_target_h > 0)
                {
                    float tx = (mouse.x - img_min.x) * ((float)m_viewport_target_w / img_size.x);
                    float ty = (mouse.y - img_min.y) * ((float)m_viewport_target_h / img_size.y);
                    gf.mouse_x = tx - (float)vp_px;
                    gf.mouse_y = ty - (float)vp_py;
                }
                else
                {
                    gf.mouse_x = 0.0f;
                    gf.mouse_y = 0.0f;
                }
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

                // Phase 34 viewport override: in Landscape Mode the brush
                // replaces the gizmo — the same mouse + camera context drives
                // the terrain pick and paint strokes.
                if (IsLandscapeSculptMode())
                    UpdateLandscapeBrush(gf, (float)dt);
                else
                    m_gizmo->Update(gf);
            }
        }
        m_profiler.EndStage(Profiler::Update);

        // Profiler: the Render stage is the off-screen 3D pass (scene render
        // + editor overlays) plus the Inspector camera preview. Skipped for
        // the minimized window, but the preview still renders.
        if (!minimized)
        {
            m_profiler.BeginStage(Profiler::Render);
            RenderViewportTarget();
            RenderCameraPreview();
            m_profiler.EndStage(Profiler::Render);
        }
        else
        {
            RenderCameraPreview();
        }

        // Inspector camera preview: render the selected camera entity into the
        // preview target after the main pass so its scene pass reuses fresh
        // AABB bounds. Drawn by the Inspector's Camera section.
        SDL_Renderer *renderer = m_window->GetNativeRenderer();
        m_profiler.BeginStage(Profiler::UI);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        m_profiler.EndStage(Profiler::UI);

        // Commit this frame's samples and record the resource snapshot:
        // live entity count, the 3D draw calls issued this frame, and an
        // estimate of resident mesh + GPU texture memory.
        size_t memory_bytes = 0;
        if (m_mesh_library)
            memory_bytes += m_mesh_library->ResidentBytes();
        if (m_texture_library)
            memory_bytes += m_texture_library->ResidentBytes();
        if (m_scene)
            memory_bytes += m_scene->GetEntities().size() * sizeof(Entity);
        m_profiler.RecordResources(
            m_scene ? (int)m_scene->GetEntities().size() : 0,
            m_draw_calls, memory_bytes);
        m_profiler.EndFrame();

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
    m_viewport_layout_panel = nullptr;
    m_landscape_panel = nullptr;
    m_timeline_panel = nullptr;
    m_profiler_panel = nullptr;

    // Tear the console pipes down before the window/SDL go away so no further
    // stdout traffic can target a closed pipe.
    Console::Instance().StopRedirect();

    if (m_viewport_target)
    {
        SDL_DestroyTexture(m_viewport_target);
        m_viewport_target = nullptr;
    }

    if (m_camera_preview)
    {
        SDL_DestroyTexture(m_camera_preview);
        m_camera_preview = nullptr;
    }

    delete m_gizmo;
    m_gizmo = nullptr;

    delete m_cameras;
    m_cameras = nullptr;

    // ScriptEngine's destructor tears down the Lua VM (and with it any
    // userdata pointing into entities), so release it before the scene.
    delete m_script_engine;
    m_script_engine = nullptr;

    // The mixer must close before the window/SDL audio device goes away; its
    // samples are pure memory, so no renderer dependency to respect here.
    if (m_audio)
    {
        m_audio->Shutdown();
        delete m_audio;
        m_audio = nullptr;
    }

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
