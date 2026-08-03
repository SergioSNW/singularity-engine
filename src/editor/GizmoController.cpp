#include "GizmoController.h"

#include "Entity.h"
#include "Mesh.h"
#include "Scene.h"
#include "SelectionState.h"

#include <SDL.h>
#include <imgui.h>

#include <cmath>
#include <cfloat>

namespace {

const float PI = 3.1415926535f;
const float AXIS_PX   = 90.0f;  // gizmo arm length on screen
const float HANDLE_PX = 10.0f;  // axis hit-test tolerance
const float CENTER_PX = 9.0f;   // center (planar) handle hit radius
const float RING_PX   = 64.0f;  // rotation ring radius
const float RING_TOL  = 9.0f;   // rotation ring hit tolerance

// Project a world point to viewport-pixel space. Returns false when the point
// is at or behind the near plane.
bool Project(const GizmoFrame &f, const Vec3 &p, float &sx, float &sy, float &depth)
{
    float w;
    Vec3 c = Mat4MulVec3(f.view_proj, p, w);
    if (w < f.near_p)
        return false;
    sx = (c.x / w + 1.0f) * 0.5f * f.vp_width;
    sy = (1.0f - c.y / w) * 0.5f * f.vp_height;
    depth = w;
    return true;
}

float DistPointSegment(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    if (l2 < 1e-6f)
    {
        float ex = px - ax, ey = py - ay;
        return std::sqrt(ex * ex + ey * ey);
    }
    float t = ((px - ax) * dx + (py - ay) * dy) / l2;
    t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
    float qx = ax + t * dx, qy = ay + t * dy;
    float ex = px - qx, ey = py - qy;
    return std::sqrt(ex * ex + ey * ey);
}

// Camera basis vectors in world space, consistent with the view matrix built
// as RotX(-pitch) * RotY(-yaw) * Translate(-pos) in Application.cpp.
void CameraBasis(float pitch, float yaw, Vec3 &right, Vec3 &up, Vec3 &fwd)
{
    float p = pitch * PI / 180.0f, y = yaw * PI / 180.0f;
    float sp = std::sin(p), cp = std::cos(p);
    float sy = std::sin(y), cy = std::cos(y);
    right = {  cy, 0.0f, -sy };
    up    = { -sy * sp, cp, -cy * sp };
    fwd   = { -sy * cp, -sp, -cy * cp };
}

struct Ray { Vec3 o, d; };

Ray MakeRay(const GizmoFrame &f)
{
    float nx = (2.0f * f.mouse_x / f.vp_width - 1.0f);
    float ny = (1.0f - 2.0f * f.mouse_y / f.vp_height);
    float tan_half = std::tan(f.cam_fov * PI / 360.0f);
    float aspect = (f.vp_height > 0.0f) ? f.vp_width / f.vp_height : 1.0f;
    Vec3 right, up, fwd;
    CameraBasis(f.cam_pitch, f.cam_yaw, right, up, fwd);
    Vec3 dir = Vec3Normalize(Vec3Add(
        Vec3Add(Vec3Scale(right, nx * tan_half * aspect),
                Vec3Scale(up, ny * tan_half)),
        Vec3Scale(fwd, -1.0f)));
    return { f.cam_pos, dir };
}

// Parameter t along the axis line (origin + t*axis) closest to the mouse ray.
bool AxisTFromRay(const Vec3 &origin, const Vec3 &axis, const Ray &r, float &t)
{
    Vec3 e = Vec3Sub(r.o, origin);
    float b = Vec3Dot(axis, r.d);
    float c3 = Vec3Dot(axis, e);
    float c4 = Vec3Dot(r.d, e);
    float denom = 1.0f - b * b; // |axis| == |d| == 1
    if (std::fabs(denom) < 1e-6f)
        return false;
    t = (b * c4 - c3) / denom;
    return true;
}

bool RayPlane(const Ray &r, const Vec3 &p, const Vec3 &n, Vec3 &hit)
{
    float denom = Vec3Dot(n, r.d);
    if (std::fabs(denom) < 1e-6f)
        return false;
    float t = Vec3Dot(n, Vec3Sub(p, r.o)) / denom;
    hit = Vec3Add(r.o, Vec3Scale(r.d, t));
    return true;
}

// Local unit axis (0=X, 1=Y, 2=Z) of a transform, in world space.
Vec3 AxisOf(const Mat4 &world, int i)
{
    return Vec3Normalize({ world.m[i * 4 + 0], world.m[i * 4 + 1], world.m[i * 4 + 2] });
}

// Convert a world-space displacement into the entity's local space (for parent
// chains, local position is expressed relative to the parent).
Vec3 WorldToLocalDir(const Entity &entity, const Scene &scene, const Vec3 &dir)
{
    if (!entity.parent)
        return dir;
    Mat4 rot = Mat4RotateOnly(scene.ComputeWorldMatrix(*entity.parent));
    return {
        rot.m[0] * dir.x + rot.m[4] * dir.y + rot.m[8] * dir.z,
        rot.m[1] * dir.x + rot.m[5] * dir.y + rot.m[9] * dir.z,
        rot.m[2] * dir.x + rot.m[6] * dir.y + rot.m[10] * dir.z,
    };
}

const Mesh* ResolveMesh(const Entity &entity, MeshLibrary *lib)
{
    if (!lib)
        return nullptr;
    if (entity.mesh.path.empty())
        return lib->GetBuiltinCube();
    std::string err;
    const Mesh *m = lib->GetOrLoad(entity.mesh.path, &err);
    return m ? m : lib->GetBuiltinCube();
}

} // namespace

void GizmoController::Update(const GizmoFrame &f)
{
    m_hover_axis = -1;
    m_hover_center = false;
    m_hover_ring = false;

    if (!f.scene || !f.selection)
        return;

    const bool lmb = ImGui::IsMouseDown(0);

    // Continue an active drag first so the selection never changes mid-drag.
    if (m_dragging)
    {
        if (!lmb)
        {
            m_dragging = false;
            m_drag_axis = -1;
            return;
        }
        Entity *sel = f.scene->GetEntityById(f.selection->entity_id);
        if (sel)
            ApplyDrag(f, *sel);
        return;
    }

    if (!f.hovered)
        return;

    Entity *sel = (f.selection->entity_id >= 0)
        ? f.scene->GetEntityById(f.selection->entity_id)
        : nullptr;

    // Hover hit-testing against the current gizmo.
    if (sel)
    {
        Mat4 world = f.scene->ComputeWorldMatrix(*sel);
        m_center = { world.m[12], world.m[13], world.m[14] };
        if (!Project(f, m_center, m_center_sx, m_center_sy, m_center_depth))
            return;

        float dist = Vec3Length(Vec3Sub(m_center, f.cam_pos));
        float tan_half = std::tan(f.cam_fov * PI / 360.0f);
        float world_per_px = (f.vp_height > 0.0f)
            ? (2.0f * dist * tan_half) / f.vp_height : 1.0f;
        m_radius_world = std::max(AXIS_PX * world_per_px, 1e-6f);
        m_axis_world[0] = AxisOf(world, 0);
        m_axis_world[1] = AxisOf(world, 1);
        m_axis_world[2] = AxisOf(world, 2);

        if (mode == GizmoMode::Rotate)
        {
            float dx = f.mouse_x - m_center_sx;
            float dy = f.mouse_y - m_center_sy;
            float dist_c = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(dist_c - RING_PX) < RING_TOL)
                m_hover_ring = true;
        }
        else
        {
            for (int i = 0; i < 3; ++i)
            {
                Vec3 tip = Vec3Add(m_center, Vec3Scale(m_axis_world[i], m_radius_world));
                float tx, ty, td;
                if (!Project(f, tip, tx, ty, td))
                    continue;
                if (DistPointSegment(f.mouse_x, f.mouse_y, m_center_sx, m_center_sy, tx, ty) < HANDLE_PX)
                    m_hover_axis = i;
            }
            float dx = f.mouse_x - m_center_sx;
            float dy = f.mouse_y - m_center_sy;
            if (std::sqrt(dx * dx + dy * dy) < CENTER_PX)
                m_hover_center = true;
        }
    }

    if (!ImGui::IsMouseClicked(0))
        return;

    if (sel && (m_hover_axis >= 0 || m_hover_center || m_hover_ring))
        StartDrag(f, *sel);
    else
        Pick(f);
}

void GizmoController::StartDrag(const GizmoFrame &f, Entity &entity)
{
    m_dragging = true;
    if (m_hover_axis >= 0)
        m_drag_axis = m_hover_axis;
    else if (m_hover_center)
        m_drag_axis = 3;
    else if (m_hover_ring)
        m_drag_axis = 4;

    m_start_pos[0] = entity.transform.position[0];
    m_start_pos[1] = entity.transform.position[1];
    m_start_pos[2] = entity.transform.position[2];
    m_start_rot[0] = entity.transform.rotation[0];
    m_start_rot[1] = entity.transform.rotation[1];
    m_start_rot[2] = entity.transform.rotation[2];
    m_start_scale[0] = entity.transform.scale[0];
    m_start_scale[1] = entity.transform.scale[1];
    m_start_scale[2] = entity.transform.scale[2];

    if (m_drag_axis == 3)
    {
        Vec3 right, up, fwd;
        CameraBasis(f.cam_pitch, f.cam_yaw, right, up, fwd);
        RayPlane(MakeRay(f), m_center, fwd, m_plane_start);
    }
    else if (m_drag_axis == 4)
    {
        m_angle_start = std::atan2(f.mouse_y - m_center_sy, f.mouse_x - m_center_sx);
    }
    else
    {
        float t;
        if (AxisTFromRay(m_center, m_axis_world[m_drag_axis], MakeRay(f), t))
            m_t_start = t;
    }
}

void GizmoController::ApplyDrag(const GizmoFrame &f, Entity &entity)
{
    if (m_drag_axis == 3)
    {
        // Planar translation: move within the plane facing the camera.
        Vec3 right, up, fwd;
        CameraBasis(f.cam_pitch, f.cam_yaw, right, up, fwd);
        Vec3 hit;
        if (RayPlane(MakeRay(f), m_center, fwd, hit))
        {
            Vec3 delta = Vec3Sub(hit, m_plane_start);
            Vec3 local = WorldToLocalDir(entity, *f.scene, delta);
            entity.transform.position[0] = m_start_pos[0] + local.x;
            entity.transform.position[1] = m_start_pos[1] + local.y;
            entity.transform.position[2] = m_start_pos[2] + local.z;
        }
    }
    else if (m_drag_axis == 4)
    {
        // Rotation: drag around the projected center, rotating about the
        // camera's view axis. delta = current angle - start angle.
        float ang = std::atan2(f.mouse_y - m_center_sy, f.mouse_x - m_center_sx);
        float delta = ang - m_angle_start;
        Vec3 right, up, fwd;
        CameraBasis(f.cam_pitch, f.cam_yaw, right, up, fwd);
        Mat4 r = Mat4Mul(
            Mat4RotateX(m_start_rot[0]),
            Mat4Mul(Mat4RotateY(m_start_rot[1]), Mat4RotateZ(m_start_rot[2])));
        Mat4 rn = Mat4Mul(Mat4RotateAxis(fwd, delta * 180.0f / PI), r);
        Vec3 e = Mat4ExtractEuler(rn);
        // Reject a corrupt decomposition: if any euler came out non-finite
        // (NaN/Inf can only appear if the start orientation was already
        // poisoned), keep the previous rotation instead of writing garbage
        // into the transform that would propagate through the renderer.
        if (std::isfinite(e.x) && std::isfinite(e.y) && std::isfinite(e.z))
        {
            entity.transform.rotation[0] = e.x;
            entity.transform.rotation[1] = e.y;
            entity.transform.rotation[2] = e.z;
        }
    }
    else if (mode == GizmoMode::Translate)
    {
        float t;
        if (AxisTFromRay(m_center, m_axis_world[m_drag_axis], MakeRay(f), t))
        {
            float delta = t - m_t_start;
            Vec3 wd = Vec3Scale(m_axis_world[m_drag_axis], delta);
            Vec3 local = WorldToLocalDir(entity, *f.scene, wd);
            entity.transform.position[0] = m_start_pos[0] + local.x;
            entity.transform.position[1] = m_start_pos[1] + local.y;
            entity.transform.position[2] = m_start_pos[2] + local.z;
        }
    }
    else if (mode == GizmoMode::Scale)
    {
        // factor = 1 + delta / radius_world converts the axis parameter
        // distance (world units) into a scale multiplier. Guard radius_world:
        // at extreme zoom it can be near zero, and a zero denominator would
        // turn the multiplier into Inf. Clamp the result to a sane range.
        float t;
        if (m_radius_world > 1e-6f &&
            AxisTFromRay(m_center, m_axis_world[m_drag_axis], MakeRay(f), t))
        {
            float factor = 1.0f + (t - m_t_start) / m_radius_world;
            float v = m_start_scale[m_drag_axis] * factor;
            if (!std::isfinite(v) || v > 10000.0f)
                v = 10000.0f;
            if (v < 0.01f)
                v = 0.01f;
            entity.transform.scale[m_drag_axis] = v;
        }
    }
}

void GizmoController::Pick(const GizmoFrame &f)
{
    int best = -1;
    float best_depth = FLT_MAX;

    for (const auto &ep : f.scene->GetEntities())
    {
        const Entity &e = *ep;
        if (e.id == f.active_camera_id)
            continue;

        Mat4 world = f.scene->ComputeWorldMatrix(e);
        const Mesh *mesh = ResolveMesh(e, f.meshes);
        if (!mesh)
            continue;

        const Vec3 &mn = mesh->bounds_min;
        const Vec3 &mx = mesh->bounds_max;
        const Vec3 corners[8] = {
            { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
            { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
            { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z },
            { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z },
        };

        float minx = FLT_MAX, miny = FLT_MAX, maxx = -FLT_MAX, maxy = -FLT_MAX;
        float depth_sum = 0.0f;
        int count = 0;
        for (int i = 0; i < 8; ++i)
        {
            float sx, sy, depth;
            float w;
            Vec3 wp = Mat4MulVec3(world, corners[i], w);
            if (!Project(f, wp, sx, sy, depth))
                continue;
            minx = std::min(minx, sx); maxx = std::max(maxx, sx);
            miny = std::min(miny, sy); maxy = std::max(maxy, sy);
            depth_sum += depth;
            ++count;
        }
        if (count == 0)
            continue;

        if (f.mouse_x < minx || f.mouse_x > maxx || f.mouse_y < miny || f.mouse_y > maxy)
            continue;

        float center_depth = depth_sum / (float)count;
        if (center_depth < best_depth)
        {
            best = e.id;
            best_depth = center_depth;
        }
    }

    if (best >= 0)
    {
        f.selection->entity_id = best;
        if (Entity *e = f.scene->GetEntityById(best))
            f.selection->entity_name = e->tag.tag;
    }
    else
    {
        f.selection->entity_id = -1;
        f.selection->entity_name.clear();
    }
}

void GizmoController::Draw(SDL_Renderer *renderer, const GizmoFrame &f)
{
    if (!f.scene || !f.selection || f.selection->entity_id < 0)
        return;
    Entity *sel = f.scene->GetEntityById(f.selection->entity_id);
    if (!sel)
        return;

    Mat4 world = f.scene->ComputeWorldMatrix(*sel);
    Vec3 center = { world.m[12], world.m[13], world.m[14] };
    float cx, cy, cd;
    if (!Project(f, center, cx, cy, cd))
        return;

    if (mode == GizmoMode::Rotate)
    {
        bool hot = (m_dragging && m_drag_axis == 4) || (!m_dragging && m_hover_ring);
        Uint8 cr = hot ? 255 : 120;
        Uint8 cg = hot ? 235 : 200;
        Uint8 cb = hot ? 235 : 255;
        SDL_SetRenderDrawColor(renderer, cr, cg, cb, 255);
        const int SEGMENTS = 64;
        float px = 0.0f, py = 0.0f;
        for (int i = 0; i <= SEGMENTS; ++i)
        {
            float a = (float)i / (float)SEGMENTS * 2.0f * PI;
            float qx = cx + std::cos(a) * RING_PX;
            float qy = cy + std::sin(a) * RING_PX;
            if (i > 0)
                SDL_RenderDrawLine(renderer, (int)px, (int)py, (int)qx, (int)qy);
            px = qx; py = qy;
        }
        if (m_dragging)
        {
            float ang = std::atan2(f.mouse_y - cy, f.mouse_x - cx);
            SDL_RenderDrawLine(renderer, (int)cx, (int)cy,
                               (int)(cx + std::cos(ang) * RING_PX),
                               (int)(cy + std::sin(ang) * RING_PX));
        }
        return;
    }

    static const Uint8 AXIS_COLOR[3][3] = {
        { 232, 80,  80  },  // X red
        { 90,  200, 100 },  // Y green
        { 80,  140, 255 },  // Z blue
    };

    Vec3 axes[3] = { AxisOf(world, 0), AxisOf(world, 1), AxisOf(world, 2) };
    for (int i = 0; i < 3; ++i)
    {
        Vec3 tip = Vec3Add(center, Vec3Scale(axes[i], m_radius_world));
        float tx, ty, td;
        if (!Project(f, tip, tx, ty, td))
            continue;

        bool hot = (m_dragging && m_drag_axis == i) || (!m_dragging && m_hover_axis == i);
        Uint8 cr = AXIS_COLOR[i][0], cg = AXIS_COLOR[i][1], cb = AXIS_COLOR[i][2];
        if (hot)
        {
            cr = (Uint8)std::min(255, cr + 60);
            cg = (Uint8)std::min(255, cg + 60);
            cb = (Uint8)std::min(255, cb + 60);
        }
        SDL_SetRenderDrawColor(renderer, cr, cg, cb, 255);
        SDL_RenderDrawLine(renderer, (int)cx, (int)cy, (int)tx, (int)ty);
        if (hot)
        {
            // Thicken the active arm with offset copies so it reads clearly.
            SDL_RenderDrawLine(renderer, (int)cx, (int)cy - 1, (int)tx, (int)ty - 1);
            SDL_RenderDrawLine(renderer, (int)cx, (int)cy + 1, (int)tx, (int)ty + 1);
        }
    }

    bool hot_center = (m_dragging && m_drag_axis == 3) || (!m_dragging && m_hover_center);
    SDL_Rect box = { (int)cx - 3, (int)cy - 3, 7, 7 };
    if (hot_center)
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    else
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderFillRect(renderer, &box);
}
