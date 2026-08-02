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
