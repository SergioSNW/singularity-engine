#include "Animation.h"

#include <algorithm>
#include <cmath>

namespace Anim
{

namespace
{

// --- Minimal quaternion math (w, x, y, z) ------------------------------------
// The engine renders Euler rotations with composite order Rx * Ry * Rz (the
// renderer applies X, then Y, then Z to a local point). A unit quaternion with
// that same composite satisfies
//     Q(q) = Rx * Ry * Rz                     (standard column-vector matrix)
// where q = qx * qy * qz is the half-angle product about each axis. The
// engine's stored matrices are transposes of the textbook standard ones, but
// that cancels out for interpolation: keyframes travel Euler -> quat -> SLERP
// -> quat -> Euler, so only the inverse pair matters, and QuatToEuler below
// inverts exactly the convention EulerToQuat produces.
struct Quat
{
    float w, x, y, z;
};

inline Quat QuatMul(const Quat &a, const Quat &b)
{
    return { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
             a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
             a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
}

inline Quat QuatNormalize(const Quat &q)
{
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-8f)
        return { 1.0f, 0.0f, 0.0f, 0.0f };
    const float inv = 1.0f / n;
    return { q.w * inv, q.x * inv, q.y * inv, q.z * inv };
}

// Shortest-arc spherical interpolation between two unit quaternions.
inline Quat QuatSlerp(const Quat &a, const Quat &b, float t)
{
    Quat qa = QuatNormalize(a);
    Quat qb = QuatNormalize(b);
    float dot = qa.w * qb.w + qa.x * qb.x + qa.y * qb.y + qa.z * qb.z;
    if (dot < 0.0f)  // take the short way around the hypersphere
    {
        qb.w = -qb.w; qb.x = -qb.x; qb.y = -qb.y; qb.z = -qb.z;
        dot = -dot;
    }
    if (dot > 0.9995f)  // near-parallel: fall back to nlerp
    {
        return QuatNormalize({ qa.w + (qb.w - qa.w) * t,
                               qa.x + (qb.x - qa.x) * t,
                               qa.y + (qb.y - qa.y) * t,
                               qa.z + (qb.z - qa.z) * t });
    }
    const float theta = std::acos(dot);
    const float s = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / s;
    const float wb = std::sin(t * theta) / s;
    return QuatNormalize({ wa * qa.w + wb * qb.w, wa * qa.x + wb * qb.x,
                           wa * qa.y + wb * qb.y, wa * qa.z + wb * qb.z });
}

// Euler XYZ (degrees) -> quaternion with composite Rx * Ry * Rz.
Quat EulerToQuat(const float e[3])
{
    const float h = 3.1415926535f / 360.0f;
    const float hx = e[0] * h, hy = e[1] * h, hz = e[2] * h;
    const Quat qx{ std::cos(hx), std::sin(hx), 0.0f, 0.0f };
    const Quat qy{ std::cos(hy), 0.0f, std::sin(hy), 0.0f };
    const Quat qz{ std::cos(hz), 0.0f, 0.0f, std::sin(hz) };
    return QuatNormalize(QuatMul(QuatMul(qx, qy), qz));
}

// Quaternion -> Euler XYZ (degrees), the exact inverse of EulerToQuat.
void QuatToEuler(const Quat &q0, float out[3])
{
    const Quat q = QuatNormalize(q0);
    const float r00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    const float r01 = 2.0f * (q.x * q.y - q.z * q.w);
    const float r02 = 2.0f * (q.x * q.z + q.y * q.w);
    const float r11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    const float r12 = 2.0f * (q.y * q.z - q.x * q.w);
    const float r21 = 2.0f * (q.y * q.z + q.x * q.w);
    const float r22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);

    const float rad_to_deg = 180.0f / 3.1415926535f;
    const float deg_to_rad = 3.1415926535f / 180.0f;

    // R = Rx * Ry * Rz: y = asin(R02), x = atan2(-R12, R22), z = atan2(-R01, R00).
    out[1] = std::asin(std::max(-1.0f, std::min(1.0f, r02))) * rad_to_deg;
    const float cy = std::cos(out[1] * deg_to_rad);
    if (std::fabs(cy) > 1e-5f)
    {
        out[0] = std::atan2(-r12, r22) * rad_to_deg;
        out[2] = std::atan2(-r01, r00) * rad_to_deg;
    }
    else  // gimbal lock: pitch at +-90 deg, fold the yaw into roll
    {
        out[2] = 0.0f;
        out[0] = std::atan2(r21, r11) * rad_to_deg;
    }
}

void CopyValue(const AnimationKeyframe &k, float out[3])
{
    out[0] = k.value[0];
    out[1] = k.value[1];
    out[2] = k.value[2];
}

} // namespace

void SetKeyframe(AnimationTrack &track, float time, const float value[3])
{
    for (auto &k : track.keys)
    {
        if (std::fabs(k.time - time) < 1e-5f)
        {
            k.value[0] = value[0];
            k.value[1] = value[1];
            k.value[2] = value[2];
            return;
        }
    }
    AnimationKeyframe kf;
    kf.time = time;
    kf.value[0] = value[0];
    kf.value[1] = value[1];
    kf.value[2] = value[2];
    track.keys.push_back(kf);
    std::sort(track.keys.begin(), track.keys.end(),
              [](const AnimationKeyframe &a, const AnimationKeyframe &b) {
                  return a.time < b.time;
              });
}

bool RemoveKeyframe(AnimationTrack &track, float time)
{
    const auto it = std::find_if(track.keys.begin(), track.keys.end(),
                                 [time](const AnimationKeyframe &k) {
                                     return std::fabs(k.time - time) < 1e-5f;
                                 });
    if (it == track.keys.end())
        return false;
    track.keys.erase(it);
    return true;
}

const AnimationKeyframe *KeyAt(const AnimationTrack &track, float time)
{
    for (const auto &k : track.keys)
        if (std::fabs(k.time - time) < 1e-5f)
            return &k;
    return nullptr;
}

float TrackDuration(const AnimationComponent &anim)
{
    return std::max(anim.position.MaxTime(),
                    std::max(anim.rotation.MaxTime(), anim.scale.MaxTime()));
}

void SampleValue(const AnimationTrack &track, float t, bool loop, float out[3])
{
    if (track.IsEmpty())
    {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float span = track.MaxTime();
    if (loop && span > 1e-6f)
        t = std::fmod(t, span);

    if (t <= track.keys.front().time)
    {
        CopyValue(track.keys.front(), out);
        return;
    }
    if (t >= track.keys.back().time)
    {
        CopyValue(track.keys.back(), out);
        return;
    }

    // Find the bracketing pair: `hi` is the first key strictly after t.
    size_t hi = 1;
    while (hi < track.keys.size() - 1 && track.keys[hi].time < t)
        ++hi;
    const AnimationKeyframe &a = track.keys[hi - 1];
    const AnimationKeyframe &b = track.keys[hi];
    const float f = (b.time > a.time) ? (t - a.time) / (b.time - a.time) : 0.0f;
    for (int i = 0; i < 3; ++i)
        out[i] = a.value[i] + (b.value[i] - a.value[i]) * f;
}

void SampleRotation(const AnimationTrack &track, float t, bool loop, float out[3])
{
    if (track.IsEmpty())
    {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float span = track.MaxTime();
    if (loop && span > 1e-6f)
        t = std::fmod(t, span);

    // Landing exactly on a key reproduces its stored Euler verbatim, so a pose
    // recorded in the Inspector is never re-expressed as an equivalent but
    // visually different angle set (the classic +-180 deg ambiguity).
    if (const AnimationKeyframe *exact = KeyAt(track, t))
    {
        CopyValue(*exact, out);
        return;
    }
    if (t <= track.keys.front().time)
    {
        CopyValue(track.keys.front(), out);
        return;
    }
    if (t >= track.keys.back().time)
    {
        CopyValue(track.keys.back(), out);
        return;
    }

    size_t hi = 1;
    while (hi < track.keys.size() - 1 && track.keys[hi].time < t)
        ++hi;
    const AnimationKeyframe &a = track.keys[hi - 1];
    const AnimationKeyframe &b = track.keys[hi];
    const float f = (b.time > a.time) ? (t - a.time) / (b.time - a.time) : 0.0f;
    QuatToEuler(QuatSlerp(EulerToQuat(a.value), EulerToQuat(b.value), f), out);
}

void Apply(const AnimationComponent &anim, float t,
           float pos[3], float rot[3], float scale[3])
{
    if (!anim.position.IsEmpty())
        SampleValue(anim.position, t, anim.loop, pos);
    if (!anim.rotation.IsEmpty())
        SampleRotation(anim.rotation, t, anim.loop, rot);
    if (!anim.scale.IsEmpty())
        SampleValue(anim.scale, t, anim.loop, scale);
}

} // namespace Anim
