#pragma once

// M8.1: keyframed Havok bodies (animated doors, lifts, moving platforms) as libsm64 surface objects that follow their
// body. [Collision] bMovingBodies, iMaxMovingBodies, iMaxMovingTriangles.

// After each Collision_AddStatics (spawn, streaming reload): replaces the objects with the nearest bodies it found.
// False if libsm64 faulted (the caller drops Mario).
bool CollisionObjects_Rebuild(bool interior, bool verbose);
// Before each sm64_mario_tick: moves every object to its body's pose; a body that is gone loses its object.
// False if libsm64 faulted.
bool CollisionObjects_Update();
// Despawn: deletes the objects in libsm64 (unless it faulted: `callLibsm64` false) and releases the bodies.
void CollisionObjects_Clear(bool callLibsm64);
// Game exit: forgets everything without touching libsm64 or the bodies (the process exit frees them).
void CollisionObjects_Abandon();
void CollisionObjects_Dump();
