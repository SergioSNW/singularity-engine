#include "PlayerController.h"

#include "Components.h"
#include "Entity.h"
#include "Landscape.h"
#include "Scene.h"

#include <algorithm>
#include <cfloat>
#include <string>

namespace
{

// `self.transform.position` is the capsule's FEET (ground contact) point,
// matching the base-pivot convention every other placeable primitive in this
// engine already uses (Wall/Floor/Ramp sit on their placement point, not
// centered on it) -- so the visual capsule mesh and this collision volume
// agree on where "the entity's position" actually is.
void PlayerAABB(const Vec3 &pos, float radius, float height, Vec3 &out_min, Vec3 &out_max)
{
    out_min = { pos.x - radius, pos.y,          pos.z - radius };
    out_max = { pos.x + radius, pos.y + height, pos.z + radius };
}

// Finds the first enabled Solid collider (other than `self`) whose world AABB
// overlaps the player's AABB at `pos`. Returns its world AABB in `out_min`/
// `out_max`, the entity itself in `out_entity`, and true on a hit.
bool FindSolidOverlap(const Entity &self, Scene &scene, const Vec3 &pos,
                      float radius, float height, Vec3 &out_min, Vec3 &out_max,
                      Entity *&out_entity)
{
    Vec3 pmin, pmax;
    PlayerAABB(pos, radius, height, pmin, pmax);
    for (auto &ep : scene.GetEntities())
    {
        if (!ep || ep.get() == &self)
            continue;
        const ColliderComponent &c = ep->collider;
        if (!c.enabled || c.type != ColliderComponent::Type::Solid)
            continue;
        const Vec3 lmin = Vec3Sub(c.center, c.extents);
        const Vec3 lmax = Vec3Add(c.center, c.extents);
        const Mat4 world = scene.ComputeWorldMatrix(*ep);
        Vec3 wmin, wmax;
        TransformAABB(lmin, lmax, world, wmin, wmax);
        if (pmax.x > wmin.x && pmin.x < wmax.x &&
            pmax.y > wmin.y && pmin.y < wmax.y &&
            pmax.z > wmin.z && pmin.z < wmax.z)
        {
            out_min = wmin;
            out_max = wmax;
            out_entity = ep.get();
            return true;
        }
    }
    return false;
}

// Resolves `pos` -> `next` against every solid collider in the scene, one
// axis at a time (X, then Z, then Y). Separating the axes is what makes a
// single generic resolver correct for all three shapes the brief names:
// a Wall's overlap is caught on the X/Z pass and only the horizontal move is
// clamped (the player still falls normally); a Floor's overlap is caught on
// the Y pass while falling and clamps the player onto its top face (a normal
// AABB "land on top" resolution, not special-cased per entity type); a Ramp
// gets the same box treatment as a Wall/Floor since ColliderComponent has no
// slope shape -- an acknowledged approximation for "basic" collision, not a
// true slope walk. `out_landed_entity` is only set (non-null) on the Y-axis
// "landed on top" case, so the caller can read what surface the footstep
// audio should match.
Vec3 ResolveEntityCollisions(const Entity &self, Scene &scene, Vec3 pos, const Vec3 &next,
                             float radius, float height, bool &landed_on_entity,
                             Entity *&out_landed_entity)
{
    landed_on_entity = false;
    out_landed_entity = nullptr;
    Vec3 wmin, wmax;
    Entity *hit = nullptr;

    // X
    {
        Vec3 trial{ next.x, pos.y, pos.z };
        if (FindSolidOverlap(self, scene, trial, radius, height, wmin, wmax, hit))
            trial.x = (next.x > pos.x) ? (wmin.x - radius) : (wmax.x + radius);
        pos.x = trial.x;
    }
    // Z
    {
        Vec3 trial{ pos.x, pos.y, next.z };
        if (FindSolidOverlap(self, scene, trial, radius, height, wmin, wmax, hit))
            trial.z = (next.z > pos.z) ? (wmin.z - radius) : (wmax.z + radius);
        pos.z = trial.z;
    }
    // Y
    {
        Vec3 trial{ pos.x, next.y, pos.z };
        if (FindSolidOverlap(self, scene, trial, radius, height, wmin, wmax, hit))
        {
            if (next.y < pos.y)
            {
                trial.y = wmax.y;   // falling onto the top face
                landed_on_entity = true;
                out_landed_entity = hit;
            }
            else
            {
                trial.y = wmin.y - height;  // rising into a ceiling
            }
        }
        pos.y = trial.y;
    }
    return pos;
}

// Nearest kLandscapePaintPalette entry to `color` by squared distance, used
// to guess a footstep sound for a landscape vertex from its painted color --
// there's no stored "which material is this" tag, only the blended RGB, so
// this is a best-effort match rather than an exact lookup.
int NearestPaletteIndex(const float color[3])
{
    int best = -1;
    float best_d2 = 0.0f;
    for (int i = 0; i < kLandscapePaintPaletteCount; ++i)
    {
        const float *pc = kLandscapePaintPalette[i].color;
        const float dx = color[0] - pc[0], dy = color[1] - pc[1], dz = color[2] - pc[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (best == -1 || d2 < best_d2)
        {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

// Exact match of an entity's assigned .mat file (e.g. from Paint Mode, see
// Application::UpdatePaintMode) to a palette entry -- more reliable than
// color-matching for a flat-shaded prop, since we know precisely which
// material was assigned rather than inferring it from a blended vertex color.
int PaletteIndexForMaterialPath(const std::string &material_path)
{
    if (material_path.empty())
        return -1;
    for (int i = 0; i < kLandscapePaintPaletteCount; ++i)
        if (material_path == kLandscapePaintPalette[i].mat_file)
            return i;
    return -1;
}

} // namespace

void PlayerControllerUpdate(Entity &self, Scene &scene,
                            const Vec3 &desired_horizontal_velocity,
                            bool jump_pressed, float dt)
{
    PlayerControllerComponent &p = self.player;
    if (!p.enabled || dt <= 0.0f)
        return;

    // No acceleration/friction modeling -- horizontal velocity snaps directly
    // to the caller's input each frame, per the "basic" brief. Vertical
    // velocity is the controller's own state, since gravity/falling has to
    // persist across frames independent of whatever the player is pressing.
    p.velocity.x = desired_horizontal_velocity.x;
    p.velocity.z = desired_horizontal_velocity.z;

    // Jump: checked against `grounded` as it stood at the end of the
    // *previous* frame (this frame hasn't re-evaluated it yet), so a jump
    // can only launch from a surface, never chain mid-air. The caller is
    // responsible for edge-triggering `jump_pressed` (once per keypress,
    // not held) -- this function doesn't debounce it.
    if (jump_pressed && p.grounded)
    {
        p.velocity.y = p.jump_speed;
        p.grounded = false;
    }

    constexpr float kGravity = 20.0f;        // world units/sec^2
    constexpr float kTerminalFall = -40.0f;  // clamp so a long drop can't tunnel through a thin floor in one frame
    p.velocity.y = std::max(p.velocity.y - kGravity * dt, kTerminalFall);

    const Vec3 pos{ self.transform.position[0], self.transform.position[1],
                    self.transform.position[2] };
    const Vec3 target = Vec3Add(pos, Vec3Scale(p.velocity, dt));

    bool landed_on_entity = false;
    Entity *landed_entity = nullptr;
    Vec3 next = ResolveEntityCollisions(self, scene, pos, target, p.radius, p.height,
                                        landed_on_entity, landed_entity);
    if (landed_on_entity)
        p.velocity.y = 0.0f;

    // Landscape ground snap: the floor of last resort, applied after entity
    // collision so a Floor entity placed above the terrain is respected --
    // this only ever raises the player up to a surface, never pushes them
    // down through one they're already standing on. Also tracks which
    // landscape/vertex actually won, purely so the footstep material lookup
    // below can sample the same spot rather than re-deriving it.
    bool grounded = landed_on_entity;
    float best_ground_y = -FLT_MAX;
    bool found_ground = false;
    LandscapeComponent *best_landscape = nullptr;
    Vec3 best_local{};
    for (auto &ep : scene.GetEntities())
    {
        if (!ep || !ep->landscape.enabled)
            continue;
        const Mat4 world = scene.ComputeWorldMatrix(*ep);
        const Vec3 local = LandscapeWorldToLocal(world, next);
        const float local_h = LandscapeSampleHeightLocal(ep->landscape, local.x, local.z);
        float w;
        const Vec3 ground_world = Mat4MulVec3(world, Vec3{ local.x, local_h, local.z }, w);
        if (!found_ground || ground_world.y > best_ground_y)
        {
            best_ground_y = ground_world.y;
            found_ground = true;
            best_landscape = &ep->landscape;
            best_local = local;
        }
    }
    bool grounded_on_landscape = false;
    if (found_ground && next.y <= best_ground_y)
    {
        next.y = best_ground_y;
        p.velocity.y = 0.0f;
        grounded = true;
        grounded_on_landscape = true;
    }
    p.grounded = grounded;

    // Footstep material: landscape wins when both a landscape and an entity
    // could apply (matches the paint/collision priority already used
    // elsewhere in this function), sampled by nearest-palette-color match at
    // the exact vertex `best_local` resolved to. Standing on a plain (never
    // painted) entity or off the edge of every landscape leaves this at -1;
    // the caller falls back to a generic footstep sound for that case.
    if (!grounded)
        p.ground_material_index = -1;
    else if (grounded_on_landscape && best_landscape &&
             !best_landscape->colors.empty())
    {
        const int res = best_landscape->resolution;
        const int stride = res + 1;
        const float cell = best_landscape->size / (float)res;
        const float half = best_landscape->size * 0.5f;
        int gc = (int)std::round((best_local.x + half) / cell);
        int gr = (int)std::round((best_local.z + half) / cell);
        gc = std::clamp(gc, 0, res);
        gr = std::clamp(gr, 0, res);
        const size_t ci = ((size_t)gr * stride + gc) * 3;
        p.ground_material_index = NearestPaletteIndex(&best_landscape->colors[ci]);
    }
    else if (landed_on_entity && landed_entity)
    {
        p.ground_material_index =
            PaletteIndexForMaterialPath(landed_entity->material.material_path);
    }
    else
    {
        p.ground_material_index = -1;
    }

    self.transform.position[0] = next.x;
    self.transform.position[1] = next.y;
    self.transform.position[2] = next.z;
}
