#pragma once

#include <cmath>

struct Vec3
{
    float x, y, z;
};

struct Mat4
{
    float m[16] = {};
};

static inline Mat4 Mat4Identity()
{
    Mat4 r;
    r.m[0] = 1.0f;  r.m[5] = 1.0f;  r.m[10] = 1.0f;  r.m[15] = 1.0f;
    return r;
}

static inline Mat4 Mat4Mul(const Mat4 &a, const Mat4 &b)
{
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a.m[k * 4 + i] * b.m[j * 4 + k];
            r.m[j * 4 + i] = sum;
        }
    return r;
}

static inline Vec3 Mat4MulVec3(const Mat4 &mat, const Vec3 &v, float &w_out)
{
    float x = mat.m[0] * v.x + mat.m[4] * v.y + mat.m[8]  * v.z + mat.m[12];
    float y = mat.m[1] * v.x + mat.m[5] * v.y + mat.m[9]  * v.z + mat.m[13];
    float z = mat.m[2] * v.x + mat.m[6] * v.y + mat.m[10] * v.z + mat.m[14];
    float w = mat.m[3] * v.x + mat.m[7] * v.y + mat.m[11] * v.z + mat.m[15];
    w_out = w;
    return {x, y, z};
}

static inline Mat4 Mat4Translate(float tx, float ty, float tz)
{
    Mat4 r = Mat4Identity();
    r.m[12] = tx;  r.m[13] = ty;  r.m[14] = tz;
    return r;
}

static inline Mat4 Mat4Scale(float sx, float sy, float sz)
{
    Mat4 r;
    r.m[0] = sx;  r.m[5] = sy;  r.m[10] = sz;  r.m[15] = 1.0f;
    return r;
}

static inline Mat4 Mat4RotateX(float deg)
{
    float rad = deg * 3.1415926535f / 180.0f;
    float c = std::cos(rad), s = std::sin(rad);
    Mat4 r = Mat4Identity();
    r.m[5] = c;   r.m[6] = s;
    r.m[9] = -s;  r.m[10] = c;
    return r;
}

static inline Mat4 Mat4RotateY(float deg)
{
    float rad = deg * 3.1415926535f / 180.0f;
    float c = std::cos(rad), s = std::sin(rad);
    Mat4 r = Mat4Identity();
    r.m[0] = c;   r.m[2] = -s;
    r.m[8] = s;   r.m[10] = c;
    return r;
}

static inline Mat4 Mat4RotateZ(float deg)
{
    float rad = deg * 3.1415926535f / 180.0f;
    float c = std::cos(rad), s = std::sin(rad);
    Mat4 r = Mat4Identity();
    r.m[0] = c;   r.m[1] = s;
    r.m[4] = -s;  r.m[5] = c;
    return r;
}

// Model matrix from position / Euler rotation (degrees, ZYX order) / scale:
// M = T * Rx * Ry * Rz * S, so a local point is scaled, then rotated, then
// translated into parent/world space.
static inline Mat4 Mat4TRS(const Vec3 &pos, const Vec3 &euler_deg, const Vec3 &scale)
{
    Mat4 rot = Mat4Mul(
        Mat4RotateX(euler_deg.x),
        Mat4Mul(Mat4RotateY(euler_deg.y), Mat4RotateZ(euler_deg.z))
    );
    Mat4 rs = Mat4Mul(rot, Mat4Scale(scale.x, scale.y, scale.z));
    return Mat4Mul(Mat4Translate(pos.x, pos.y, pos.z), rs);
}

static inline Mat4 Mat4Transpose(const Mat4 &m)
{
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[j * 4 + i] = m.m[i * 4 + j];
    return r;
}

static inline Mat4 Mat4Ortho(float left, float right, float bottom, float top, float near_, float far_)
{
    Mat4 r;
    r.m[0]  = 2.0f / (right - left);
    r.m[5]  = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (far_ - near_);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(far_ + near_) / (far_ - near_);
    r.m[15] = 1.0f;
    return r;
}

static inline Mat4 Mat4Perspective(float fov_deg, float aspect, float near_, float far_)
{
    float f = 1.0f / std::tan(fov_deg * 3.1415926535f / 360.0f);
    float range_inv = 1.0f / (near_ - far_);
    Mat4 r;
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (far_ + near_) * range_inv;
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * far_ * near_ * range_inv;
    r.m[15] = 0.0f;
    return r;
}

// --- Vector helpers (used by gizmo math and mesh pipeline) ---

static inline Vec3 Vec3Add(const Vec3 &a, const Vec3 &b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

static inline Vec3 Vec3Sub(const Vec3 &a, const Vec3 &b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

static inline Vec3 Vec3Scale(const Vec3 &v, float s)
{
    return { v.x * s, v.y * s, v.z * s };
}

static inline float Vec3Dot(const Vec3 &a, const Vec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 Vec3Cross(const Vec3 &a, const Vec3 &b)
{
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}

static inline float Vec3Length(const Vec3 &v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline Vec3 Vec3Normalize(const Vec3 &v)
{
    float len = Vec3Length(v);
    if (len < 1e-8f)
        return { 0.0f, 0.0f, 0.0f };
    float inv = 1.0f / len;
    return { v.x * inv, v.y * inv, v.z * inv };
}

// Right-handed axis-angle rotation matrix (matches the convention of the
// Mat4RotateX/Y/Z helpers: positive angles rotate X -> Y -> Z). `axis` is
// normalized internally, so it may be passed un-normalized.
static inline Mat4 Mat4RotateAxis(Vec3 axis, float deg)
{
    axis = Vec3Normalize(axis);
    float rad = deg * 3.1415926535f / 180.0f;
    float c = std::cos(rad), s = std::sin(rad);
    float ux = axis.x, uy = axis.y, uz = axis.z;
    float k = 1.0f - c;

    Mat4 r = Mat4Identity();
    r.m[0]  = c + ux * ux * k;
    r.m[1]  = ux * uy * k + s * uz;
    r.m[2]  = ux * uz * k - s * uy;
    r.m[4]  = ux * uy * k - s * uz;
    r.m[5]  = c + uy * uy * k;
    r.m[6]  = uy * uz * k + s * ux;
    r.m[8]  = ux * uz * k + s * uy;
    r.m[9]  = uy * uz * k - s * ux;
    r.m[10] = c + uz * uz * k;
    return r;
}

// Extract the pure rotation of a transform (world) matrix. Column scaling
// (from local or inherited parent scale) is removed by normalizing each of the
// three rotation columns, so the result is orthonormal.
static inline Mat4 Mat4RotateOnly(const Mat4 &m)
{
    Mat4 r = Mat4Identity();
    for (int col = 0; col < 3; ++col)
    {
        Vec3 v = { m.m[col * 4 + 0], m.m[col * 4 + 1], m.m[col * 4 + 2] };
        Vec3 n = Vec3Normalize(v);
        r.m[col * 4 + 0] = n.x;
        r.m[col * 4 + 1] = n.y;
        r.m[col * 4 + 2] = n.z;
    }
    return r;
}

// Recover Euler angles (degrees, X/Y/Z = rotation[0/1/2]) from an orthonormal
// rotation matrix built as M = Rx(rot.x) * Ry(rot.y) * Rz(rot.z). This is the
// inverse of the Mat4TRS rotation composition. Derived directly from the matrix
// entries (gimbal lock at |rot.y| ~= 90 deg is unhandled but degrades smoothly).
static inline Vec3 Mat4ExtractEuler(const Mat4 &m)
{
    const float rad2deg = 180.0f / 3.1415926535f;
    float sy = m.m[8]; // M[0][2] = sin(rot.y)
    if (sy >  1.0f) sy =  1.0f;
    if (sy < -1.0f) sy = -1.0f;
    float rot_y = std::asin(sy);
    float rot_z = std::atan2(-m.m[4], m.m[0]);
    float rot_x = std::atan2(-m.m[9], m.m[10]);
    return { rot_x * rad2deg, rot_y * rad2deg, rot_z * rad2deg };
}

static inline Mat4 Mat4LookAt(const Vec3 &eye, const Vec3 &target, const Vec3 &up)
{
    Vec3 fwd = { target.x - eye.x, target.y - eye.y, target.z - eye.z };
    float fwd_len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fwd_len < 1e-8f) return Mat4Identity();
    fwd.x /= fwd_len;  fwd.y /= fwd_len;  fwd.z /= fwd_len;

    Vec3 rgt = {
        up.y * fwd.z - up.z * fwd.y,
        up.z * fwd.x - up.x * fwd.z,
        up.x * fwd.y - up.y * fwd.x
    };
    float rgt_len = std::sqrt(rgt.x * rgt.x + rgt.y * rgt.y + rgt.z * rgt.z);
    if (rgt_len < 1e-8f) return Mat4Identity();
    rgt.x /= rgt_len;  rgt.y /= rgt_len;  rgt.z /= rgt_len;

    Vec3 new_up = {
        rgt.y * fwd.z - rgt.z * fwd.y,
        rgt.z * fwd.x - rgt.x * fwd.z,
        rgt.x * fwd.y - rgt.y * fwd.x
    };

    Mat4 r;
    r.m[0] = rgt.x;    r.m[4] = rgt.y;    r.m[8]  = rgt.z;
    r.m[1] = new_up.x; r.m[5] = new_up.y; r.m[9]  = new_up.z;
    r.m[2] = -fwd.x;   r.m[6] = -fwd.y;   r.m[10] = -fwd.z;
    r.m[15] = 1.0f;

    r.m[12] = -(r.m[0]*eye.x + r.m[4]*eye.y + r.m[8]*eye.z);
    r.m[13] = -(r.m[1]*eye.x + r.m[5]*eye.y + r.m[9]*eye.z);
    r.m[14] = -(r.m[2]*eye.x + r.m[6]*eye.y + r.m[10]*eye.z);
    return r;
}

// --- Ray / AABB intersection (slab method) ---

// Intersect a ray with an axis-aligned box. `dir` need not be normalized; the
// returned parameters are distances in units of `dir` from `origin`. A hit
// where the box contains the ray origin yields t_near == 0. The box is
// rejected when it lies entirely behind the ray origin (tmax < 0) or the ray
// is parallel to a slab the origin is outside of.
static inline bool RayAABB(const Vec3 &o, const Vec3 &d, const Vec3 &min, const Vec3 &max,
                           float &t_near, float &t_far)
{
    float tmin = 0.0f, tmax = 1e30f;
    const float origin[3] = { o.x, o.y, o.z };
    const float dir[3]    = { d.x, d.y, d.z };
    const float lo[3]     = { min.x, min.y, min.z };
    const float hi[3]     = { max.x, max.y, max.z };

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::fabs(dir[axis]) < 1e-12f)
        {
            // Ray parallel to this slab: hit only if the origin is inside it.
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis])
                return false;
            continue;
        }
        float inv = 1.0f / dir[axis];
        float t1 = (lo[axis] - origin[axis]) * inv;
        float t2 = (hi[axis] - origin[axis]) * inv;
        if (t1 > t2)
        {
            float tmp = t1; t1 = t2; t2 = tmp;
        }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax)
            return false;
    }

    t_near = tmin;
    t_far = tmax;
    return true;
}

// World-space AABB of a local-space box transformed by `world`. Transforming
// the 8 corners and taking the min/max is exact for an axis-aligned box under
// an arbitrary affine transform (rotation/scale/shear), so it is used to
// derive an entity's world bounds from its mesh's local bounds.
static inline void TransformAABB(const Vec3 &local_min, const Vec3 &local_max,
                                 const Mat4 &world, Vec3 &out_min, Vec3 &out_max)
{
    const float x0 = local_min.x, y0 = local_min.y, z0 = local_min.z;
    const float x1 = local_max.x, y1 = local_max.y, z1 = local_max.z;
    const float xs[2] = { x0, x1 }, ys[2] = { y0, y1 }, zs[2] = { z0, z1 };

    out_min = { 1e30f, 1e30f, 1e30f };
    out_max = { -1e30f, -1e30f, -1e30f };

    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
            {
                float w;
                Vec3 p = Mat4MulVec3(world, { xs[i], ys[j], zs[k] }, w);
                if (p.x < out_min.x) out_min.x = p.x;
                if (p.y < out_min.y) out_min.y = p.y;
                if (p.z < out_min.z) out_min.z = p.z;
                if (p.x > out_max.x) out_max.x = p.x;
                if (p.y > out_max.y) out_max.y = p.y;
                if (p.z > out_max.z) out_max.z = p.z;
            }
}
