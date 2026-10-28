#pragma once

#include "libsm64.h"

// Mario's mesh in the game's scene graph: an unlit vertex-colour NiTriShape plus an overlay shape holding only the
// textured triangles (atlas x white, alpha-blended), both under our own NiNode attached to the world ShadowSceneNode,
// rebuilt from the latest tick (no interpolation yet).

// Called when Mario spawns. Logs every stage; on failure the render stays off until the next spawn.
void MarioRender_Create();
// Copies the latest libsm64 geometry (world SM64 units) into the shape, relative to Mario's position.
void MarioRender_Update(const SM64MarioGeometryBuffers& geometry, const float marioPos[3]);
// Detaches and releases the mesh. Safe to call when nothing exists.
void MarioRender_Destroy(const char* reason);
// Process exit: forget the objects without calling into the game.
void MarioRender_Abandon();
void MarioRender_DumpState();
