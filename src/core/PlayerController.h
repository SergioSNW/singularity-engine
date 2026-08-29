#pragma once

#include "EngineMath.h"

struct Entity;
class Scene;

// Stage 2 character controller: pure gameplay logic (no input, no rendering,
// no editor state), so it can run headlessly the same way Landscape.cpp does.
// Application.cpp owns turning WASD + camera yaw into `desired_horizontal_
// velocity` and calls this once per Play-mode frame.

// Advance `self`'s PlayerControllerComponent for one frame: applies a jump
// impulse if `jump_pressed` is true and the player was grounded as of the
// end of the previous frame, then gravity, then snaps horizontal velocity
// to `desired_horizontal_velocity` (world-space, Y ignored -- no
// acceleration/friction modeling, this is a "basic" brief), resolves
// per-axis AABB collision against every OTHER entity's enabled Solid
// collider (so walls block, floors/ramps support -- see the .cpp for why
// one axis-separated resolver handles all three), then snaps to the
// nearest landscape surface below the resolved position if one exists, and
// finally re-derives `ground_material_index` (a best-effort match against
// Landscape.h's shared paint palette, by vertex color on a landscape or by
// exact .mat path on an entity) for Application.cpp's footstep audio to
// read. Does nothing if `self.player.enabled` is false. Mutates
// `self.transform.position` and `self.player.velocity`/`.grounded`/
// `.ground_material_index`.
void PlayerControllerUpdate(Entity &self, Scene &scene,
                            const Vec3 &desired_horizontal_velocity,
                            bool jump_pressed, float dt);
