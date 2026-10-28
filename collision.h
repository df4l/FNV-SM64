#pragma once

#include <vector>
#include "libsm64.h"

class PlayerCharacter;

// M5.1: static collision from the exterior terrain. The land vertices (collision.md §5) of the cell under `centre`
// (game units) and its 8 neighbours become SM64 triangles. [Collision] bTerrain.
// At spawn (reload false), returns false (and leaves `out` empty) in interiors, when no terrain mesh was read, or when
// the terrain height under the centre doesn't match the player's feet; the caller then uses the test floor.
// On a reload (M5.3) only an empty result fails, and it logs less.
bool Collision_BuildTerrain(const PlayerCharacter* player, const float centre[3], bool reload, std::vector<SM64Surface>& out);
// M5.2: appends the fixed Havok bodies of the refs (collision.md §2-§4) of the loaded grid (exteriors) or the player's
// cell (interiors, M5.4) within [Collision] fStaticRadius of `centre`. [Collision] bStatics. `verbose` logs everything.
void Collision_AddStatics(const PlayerCharacter* player, const float centre[3], bool verbose, std::vector<SM64Surface>& out);
// M8.1: a keyframed body (animated door, lift, moving platform) or, M8.2, a dynamic one (crate, barrel, furniture)
// within fStaticRadius, found by the last Collision_AddStatics instead of being skipped. [Collision] bMovingBodies,
// bDynamicBodies.
struct MovingBody
{
	void* worldObject;		// bhkRigidBody (NiRefObject)
	const void* body;		// its hkpRigidBody
	uint32_t refID, baseID;
	uint8_t baseType, layer, motion;
	bool door;
	bool dynamic;			// simulated (M8.2), not keyframed
	float dist;				// horizontal, from the build centre (game units)
};
// Body pose: rot[j] = image of local axis j (FNV frame), pos in game units.
struct BodyPose
{
	float rot[3][3];
	float pos[3];
};
const std::vector<MovingBody>& Collision_MovingBodies();
// False if the body left the Havok world, its wrapper/vtables changed, or reading faulted.
bool Collision_ReadBodyPose(const MovingBody& body, BodyPose* pose);
// The body's shape as surfaces relative to its current pose (local axes mapped like Units::FnvToSm64, SM64 units),
// oriented like the statics; at most maxTriangles. False (and `out` empty) on the same failures as above.
bool Collision_BodySurfaces(const MovingBody& body, bool interior, uint32_t maxTriangles, std::vector<SM64Surface>& out,
	BodyPose* pose);
// Interiors (no terrain): resets what F10 reports as the ground source.
void Collision_ClearTerrain(const char* source);
// M5.4: height (SM64 units) of the floor-facing surface under pos (SM64 units) closest to pos's height; false if none.
bool Collision_NearestFloor(const std::vector<SM64Surface>& surfaces, const float pos[3], float* outY);
void Collision_DumpState();
void Collision_DumpStatics();
// M5.4c: logs the static surfaces above/below `feet` (game units) and the walls near Mario, with their source refs.
void Collision_DumpNearby(const float feet[3], const char* reason);
