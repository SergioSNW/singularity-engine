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
const float AXIS_PX   = 90.0f;  // gizmo arm length on screen (logical points)
const float HANDLE_PX = 10.0f;  // axis hit-test tolerance (logical points)
const float CENTER_PX = 9.0f;   // center (planar) handle hit radius
const float RING_PX   = 84.0f;  // outer trackball ring radius (logical points)
const float RING_TOL  = 9.0f;   // rotation ring hit tolerance (logical points)
const float RING_SCALE = 0.78f; // 3D axis ring radius, as a fraction of the arm length

// Orthonormal basis (u, v, n) for the plane perpendicular to n. The frame is
// right-handed: cross(u, v) == n. Used to build ring points and to measure a
// cursor angle around a ring's axis.
void RingBasis(const Vec3 &n, Vec3 &u, Vec3 &v)
{
    Vec3 ref = (std::fabs(n.x) < 0.9f) ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    u = Vec3Normalize(Vec3Cross(ref, n));
    v = Vec3Cross(n, u);
}

Vec3 RingPoint(const Vec3 &center, const Vec3 &u, const Vec3 &v, float radius, float a)
{
    return Vec3Add(center, Vec3Add(Vec3Scale(u, std::cos(a) * radius),
                                   Vec3Scale(v, std::sin(a) * radius)));
}

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
    right = { cy, 0.0f, -sy };
    up    = { sp * sy, cp, sp * cy };
    fwd   = { cp * sy, -sp, cp * cy };
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

// Minimum screen-space distance from the cursor to a ring's projected ellipse.
// The ring is sampled as N world-space points and tested as a polyline, which
// is robust for any viewing angle (the ellipse may collapse to a line).
float RingScreenDistance(const GizmoFrame &f, const Vec3 &center, const Vec3 &normal,
                         float radius)
{
    Vec3 u, v;
    RingBasis(normal, u, v);
    const int SEG = 48;
    float best = FLT_MAX;
    float px = 0.0f, py = 0.0f;
    bool have = false;
    for (int i = 0; i <= SEG; ++i)
    {
        float a = (float)i / (float)SEG * 2.0f * PI;
        Vec3 pt = RingPoint(center, u, v, radius, a);
        float sx, sy, d;
        if (!Project(f, pt, sx, sy, d))
        {
            have = false;
            continue;
        }
        if (have)
            best = std::min(best, DistPointSegment(f.mouse_x, f.mouse_y, px, py, sx, sy));
        px = sx; py = sy;
        have = true;
    }
    return best;
}

// Draw a 3D rotation ring: a circle in the plane perpendicular to `normal`,
// drawn as a projected polyline. The half facing the camera is bright, the far
// half is dimmed so the ring's 3D orientation reads at any angle.
void DrawRing3D(SDL_Renderer *renderer, const GizmoFrame &f, const Vec3 &center,
                const Vec3 &normal, float radius, const Uint8 color[3], bool hot)
{
    Vec3 u, v;
    RingBasis(normal, u, v);
    const int SEG = 48;
    float px = 0.0f, py = 0.0f;
    bool have = false;
    for (int i = 0; i <= SEG; ++i)
    {
        float a = (float)i / (float)SEG * 2.0f * PI;
        Vec3 pt = RingPoint(center, u, v, radius, a);
        float sx, sy, d;
        if (!Project(f, pt, sx, sy, d))
        {
            have = false;
            continue;
        }

        bool front = Vec3Dot(Vec3Sub(pt, center), Vec3Sub(f.cam_pos, center)) >= 0.0f;
        Uint8 cr = color[0], cg = color[1], cb = color[2];
        if (hot)
        {
            cr = (Uint8)std::min(255, cr + 70);
            cg = (Uint8)std::min(255, cg + 70);
            cb = (Uint8)std::min(255, cb + 70);
        }
        if (!front)
        {
            cr = (Uint8)(cr * 0.30f);
            cg = (Uint8)(cg * 0.30f);
            cb = (Uint8)(cb * 0.30f);
        }

        if (have)
        {
            SDL_SetRenderDrawColor(renderer, cr, cg, cb, 255);
            SDL_RenderDrawLine(renderer, (int)px, (int)py, (int)sx, (int)sy);
        }
        px = sx; py = sy;
        have = true;
    }
}

// Compose a delta rotation (degrees about a world-space axis) on top of the
// drag-start orientation, decompose back to euler, and write it into the
// entity. Non-finite euler output is rejected so a poisoned start orientation
// can never propagate garbage into the renderer.
void ApplyRotationAboutAxis(Entity &entity, const Vec3 &axis, float delta_deg,
                            const float start_rot[3])
{
    Mat4 r = Mat4Mul(
        Mat4RotateX(start_rot[0]),
        Mat4Mul(Mat4RotateY(start_rot[1]), Mat4RotateZ(start_rot[2])));
    Mat4 rn = Mat4Mul(Mat4RotateAxis(axis, delta_deg), r);
    Vec3 e = Mat4ExtractEuler(rn);
    if (std::isfinite(e.x) && std::isfinite(e.y) && std::isfinite(e.z))
    {
        entity.transform.rotation[0] = e.x;
        entity.transform.rotation[1] = e.y;
        entity.transform.rotation[2] = e.z;
    }
}

// Local unit axis (0=X, 1=Y, 2=Z) of a transform, in world space.
Vec3 AxisOf(const Mat4 &world, int i)
{
    return Vec3Normalize({ world.m[i * 4 + 0], world.m[i * 4 + 1], world.m[i * 4 + 2] });
}

// Round `v` to the nearest multiple of `step`. A non-positive step disables
// snapping (identity). Applied to the final value edited by a gizmo drag, so a
// snap lands exactly on the grid instead of accumulating drift from the raw
// mouse delta.
float SnapValue(float v, float step)
{
    return (step > 0.0f) ? std::round(v / step) * step : v;
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
    m_hover_ring_axis = -1;
    m_hover_entity = -1;

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

    // The entity under the cursor (ray / world-AABB test) drives the hover
    // bounds box and is reported to the editor; selection is left untouched.
    m_hover_entity = RaycastEntity(f);

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
        m_radius_world = std::max(AXIS_PX * f.dpi_scale * world_per_px, 1e-6f);
        m_axis_world[0] = AxisOf(world, 0);
        m_axis_world[1] = AxisOf(world, 1);
        m_axis_world[2] = AxisOf(world, 2);

        if (mode == GizmoMode::Rotate)
        {
            // Three orthogonal 3D rings, one per local axis. The cursor is
            // tested against each ring's projected ellipse; the first ring
            // within tolerance wins (axis-specific rotation beats the outer
            // trackball when they overlap).
            const float ring_radius = m_radius_world * RING_SCALE;
            const float ring_tol = RING_TOL * f.dpi_scale;
            for (int i = 0; i < 3 && m_hover_ring_axis < 0; ++i)
            {
                if (RingScreenDistance(f, m_center, m_axis_world[i], ring_radius) < ring_tol)
                    m_hover_ring_axis = i;
            }

            // Outer screen-facing trackball ring.
            if (m_hover_ring_axis < 0)
            {
                float dx = f.mouse_x - m_center_sx;
                float dy = f.mouse_y - m_center_sy;
                if (std::fabs(std::sqrt(dx * dx + dy * dy) - RING_PX * f.dpi_scale) < ring_tol)
                    m_hover_ring_axis = 3;
            }
        }
        else
        {
            const float handle_px = HANDLE_PX * f.dpi_scale;
            for (int i = 0; i < 3; ++i)
            {
                Vec3 tip = Vec3Add(m_center, Vec3Scale(m_axis_world[i], m_radius_world));
                float tx, ty, td;
                if (!Project(f, tip, tx, ty, td))
                    continue;
                if (DistPointSegment(f.mouse_x, f.mouse_y, m_center_sx, m_center_sy, tx, ty) < handle_px)
                    m_hover_axis = i;
            }
            float dx = f.mouse_x - m_center_sx;
            float dy = f.mouse_y - m_center_sy;
            if (std::sqrt(dx * dx + dy * dy) < CENTER_PX * f.dpi_scale)
                m_hover_center = true;
        }
    }

    if (!ImGui::IsMouseClicked(0))
        return;

    if (sel && (m_hover_axis >= 0 || m_hover_center || m_hover_ring_axis >= 0))
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
    else if (m_hover_ring_axis >= 0)
        m_drag_axis = (m_hover_ring_axis == 3) ? 4 : m_hover_ring_axis;

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
        // Outer trackball: angle of the cursor around the projected center.
        m_angle_start = std::atan2(f.mouse_y - m_center_sy, f.mouse_x - m_center_sx);
    }
    else if (mode == GizmoMode::Rotate)
    {
        // 3D ring: intersect the mouse ray with the ring's plane and measure
        // the initial cursor angle around the ring axis.
        Vec3 u, v;
        RingBasis(m_axis_world[m_drag_axis], u, v);
        Vec3 hit;
        if (RayPlane(MakeRay(f), m_center, m_axis_world[m_drag_axis], hit))
        {
            Vec3 d = Vec3Sub(hit, m_center);
            m_angle_start = std::atan2(Vec3Dot(d, v), Vec3Dot(d, u));
        }
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
            entity.transform.position[0] = f.snap_active
                ? SnapValue(m_start_pos[0] + local.x, f.snap_translation)
                : m_start_pos[0] + local.x;
            entity.transform.position[1] = f.snap_active
                ? SnapValue(m_start_pos[1] + local.y, f.snap_translation)
                : m_start_pos[1] + local.y;
            entity.transform.position[2] = f.snap_active
                ? SnapValue(m_start_pos[2] + local.z, f.snap_translation)
                : m_start_pos[2] + local.z;
        }
    }
    else if (m_drag_axis == 4)
    {
        // Outer trackball: free rotation about the camera's view axis, driven
        // by the cursor angle around the projected center.
        float ang = std::atan2(f.mouse_y - m_center_sy, f.mouse_x - m_center_sx);
        float delta_deg = (ang - m_angle_start) * 180.0f / PI;
        if (f.snap_active)
            delta_deg = SnapValue(delta_deg, f.snap_rotation);
        Vec3 right, up, fwd;
        CameraBasis(f.cam_pitch, f.cam_yaw, right, up, fwd);
        ApplyRotationAboutAxis(entity, fwd, delta_deg, m_start_rot);
    }
    else if (mode == GizmoMode::Rotate)
    {
        // 3D ring: rotate about the ring's world-space axis. The cursor angle
        // is measured in the ring's plane (u/v basis) and delta is wrapped to
        // [-PI, PI] so the object tracks the cursor without snapping at the
        // atan2 discontinuity. Snap rounds the delta to the configured step.
        Vec3 n = m_axis_world[m_drag_axis];
        Vec3 u, v;
        RingBasis(n, u, v);
        Vec3 hit;
        if (RayPlane(MakeRay(f), m_center, n, hit))
        {
            Vec3 d = Vec3Sub(hit, m_center);
            float ang = std::atan2(Vec3Dot(d, v), Vec3Dot(d, u));
            float delta = ang - m_angle_start;
            while (delta >  PI) delta -= 2.0f * PI;
            while (delta < -PI) delta += 2.0f * PI;
            float delta_deg = delta * 180.0f / PI;
            if (f.snap_active)
                delta_deg = SnapValue(delta_deg, f.snap_rotation);
            ApplyRotationAboutAxis(entity, n, delta_deg, m_start_rot);
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
            if (f.snap_active)
            {
                entity.transform.position[0] = SnapValue(m_start_pos[0] + local.x, f.snap_translation);
                entity.transform.position[1] = SnapValue(m_start_pos[1] + local.y, f.snap_translation);
                entity.transform.position[2] = SnapValue(m_start_pos[2] + local.z, f.snap_translation);
            }
            else
            {
                entity.transform.position[0] = m_start_pos[0] + local.x;
                entity.transform.position[1] = m_start_pos[1] + local.y;
                entity.transform.position[2] = m_start_pos[2] + local.z;
            }
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
            if (f.snap_active)
                v = SnapValue(v, f.snap_scale);
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
    int best = RaycastEntity(f);

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

// Raycast the cursor ray (MakeRay) against every entity's world-space AABB and
// return the id of the nearest hit, or -1. Exact 3D intersection: unlike a
// screen-space rectangle test, overlapping boxes are resolved by true depth
// along the ray instead of by projected-corner averages.
int GizmoController::RaycastEntity(const GizmoFrame &f)
{
    if (!f.scene || !f.meshes)
        return -1;

    Ray ray = MakeRay(f);
    int best = -1;
    float best_t = FLT_MAX;

    for (const auto &ep : f.scene->GetEntities())
    {
        const Entity &e = *ep;
        if (e.id == f.active_camera_id)
            continue;

        const Mesh *mesh = ResolveMesh(e, f.meshes);
        if (!mesh)
            continue;

        Mat4 world = f.scene->ComputeWorldMatrix(e);
        Vec3 wmin, wmax;
        TransformAABB(mesh->bounds_min, mesh->bounds_max, world, wmin, wmax);

        float t_near, t_far;
        if (RayAABB(ray.o, ray.d, wmin, wmax, t_near, t_far) && t_near < best_t)
        {
            best = e.id;
            best_t = t_near;
        }
    }

    return best;
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
        const float ring_px = RING_PX * f.dpi_scale;
        const float ring_radius = m_radius_world * RING_SCALE;

        // While dragging an axis ring, freeze the gizmo at the drag-start
        // orientation so the ring stays fixed in space while the object
        // rotates beneath it (the dragged ring IS the axis of rotation).
        const bool freeze = m_dragging && m_drag_axis >= 0 && m_drag_axis <= 2;
        Vec3 axes[3];
        if (freeze)
        {
            axes[0] = m_axis_world[0];
            axes[1] = m_axis_world[1];
            axes[2] = m_axis_world[2];
        }
        else
        {
            axes[0] = AxisOf(world, 0);
            axes[1] = AxisOf(world, 1);
            axes[2] = AxisOf(world, 2);
        }

        int active = m_dragging ? ((m_drag_axis == 4) ? 3 : m_drag_axis) : m_hover_ring_axis;

        static const Uint8 AXIS_COLOR[3][3] = {
            { 232, 80,  80  },  // X red
            { 90,  200, 100 },  // Y green
            { 80,  140, 255 },  // Z blue
        };
        for (int i = 0; i < 3; ++i)
            DrawRing3D(renderer, f, center, axes[i], ring_radius, AXIS_COLOR[i], active == i);

        // Outer screen-facing trackball ring.
        Uint8 or_c = 200, og = 235, ob = 255;
        if (active == 3)
        {
            or_c = 255; og = 255; ob = 255;
        }
        SDL_SetRenderDrawColor(renderer, or_c, og, ob, 255);
        const int SEGMENTS = 64;
        float px = 0.0f, py = 0.0f;
        for (int i = 0; i <= SEGMENTS; ++i)
        {
            float a = (float)i / (float)SEGMENTS * 2.0f * PI;
            float qx = cx + std::cos(a) * ring_px;
            float qy = cy + std::sin(a) * ring_px;
            if (i > 0)
                SDL_RenderDrawLine(renderer, (int)px, (int)py, (int)qx, (int)qy);
            px = qx; py = qy;
        }
        if (m_dragging)
        {
            // Radius indicator on the active ring tracks the cursor.
            if (m_drag_axis == 4)
            {
                float ang = std::atan2(f.mouse_y - cy, f.mouse_x - cx);
                SDL_RenderDrawLine(renderer, (int)cx, (int)cy,
                                   (int)(cx + std::cos(ang) * ring_px),
                                   (int)(cy + std::sin(ang) * ring_px));
            }
            else if (m_drag_axis >= 0 && m_drag_axis <= 2)
            {
                Vec3 n = m_axis_world[m_drag_axis];
                Vec3 u, v;
                RingBasis(n, u, v);
                Vec3 hit;
                if (RayPlane(MakeRay(f), center, n, hit))
                {
                    Vec3 d = Vec3Sub(hit, center);
                    float ang = std::atan2(Vec3Dot(d, v), Vec3Dot(d, u));
                    Vec3 pt = RingPoint(center, u, v, ring_radius, ang);
                    float sx, sy, sd;
                    if (Project(f, pt, sx, sy, sd))
                        SDL_RenderDrawLine(renderer, (int)cx, (int)cy, (int)sx, (int)sy);
                }
            }
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
