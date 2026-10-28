#include "mario.h"
#include "sm64_loader.h"
#include "units.h"
#include "config.h"
#include "game_addrs.h"
#include "player_control.h"
#include "control_filter.h"
#include "player_hide.h"
#include "mario_render.h"
#include "mario_input.h"
#include "camera.h"
#include "collision.h"
#include "collision_objects.h"
#include "audio.h"
#include "gameplay.h"
#include "npc_colliders.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/GameForms.h"
#include "nvse/NiPoint.h"
#include "nvse/Utilities.h"
#include "decomp/include/sm64shared.h"
#include "decomp/include/surface_terrains.h"

#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
	// Test floor (M2; still used in interiors and when the terrain is off or rejected): two triangles, half-width as
	// in SA's loadCollisions.
	constexpr int32_t kFloorHalfWidth = 16384;
	// Mario starts this far above the floor so the log shows a fall and a landing (~2 m).
	constexpr float kSpawnDropFnv = 140.0f;
	// Interiors (M5.4): a test-size floor this far under the lowest static triangle catches Mario if he falls through a
	// gap; the statics' floor under the player must be within kFloorToleranceFnv of their feet, else the test floor.
	constexpr float kSafetyFloorDropFnv = 512.0f;
	constexpr float kFloorToleranceFnv = 64.0f;
	constexpr uint32_t kLogEveryTicks = 30;
	// Player farther than this from where we put them last frame = game teleport (fast travel in the same worldspace,
	// COC, scripted MoveTo). One frame of vanilla movement is far below it (~10 units at sprint speed and 60 fps).
	constexpr float kTeleportDistFnv = 256.0f;
	// No floor under Mario (SA's FLOOR_LOWER_LIMIT check) also rebuilds, but only after he moved this far from the
	// last build centre, checked every kNoFloorCheckTicks ticks, so a fall into a void doesn't rebuild every tick.
	constexpr float kNoFloorMinMoveFnv = 256.0f;
	constexpr uint32_t kNoFloorCheckTicks = 30;

	int32_t s_marioId = -1;
	// Interior cell or exterior worldspace Mario was spawned in; the player leaving it (door, fast travel) despawns him.
	const void* s_spawnSpace = nullptr;
	uint32_t s_followFrames = 0;
	NiPoint3 s_lastFollowPos{};
	bool s_spawnFailed = false;	// no retry until the toggle is cycled, to avoid log spam
	uint32_t s_marioTicks = 0;
	uint32_t s_lastAction = 0;
	// sm64_mario_tick cost over a 30-tick window (static triangle count makes it grow: libsm64 scans them all).
	double s_tickMsSum = 0.0, s_tickMsWindowMax = 0.0, s_tickMsAvg = 0.0, s_tickMsMax = 0.0;
	bool s_meshDirty = false;	// a tick produced geometry the render hasn't taken yet
	// M5.3 collision streaming: centre (game units) of the loaded terrain + statics, rebuilt around Mario as he moves.
	bool s_streaming = false;	// spawned on the terrain (exteriors); the test floor isn't streamed
	float s_collisionCentre[3]{};
	uint32_t s_ticksSinceReload = 0;
	uint32_t s_reloadRetryDelay = 0;	// ticks to wait after a failed build before trying again
	uint32_t s_reloads = 0, s_reloadsFailed = 0;
	double s_lastReloadMs = 0.0, s_maxReloadMs = 0.0;
	const char* s_lastReloadReason = "none";
	std::vector<SM64Surface> s_surfaces;
	SM64MarioInputs s_inputs{};
	SM64MarioState s_state{};
	SM64MarioGeometryBuffers s_geometry{};
	std::vector<float> s_position, s_normal, s_color, s_uv;
	// The tick before the latest one, for frame interpolation (SA marioLastPos / marioLastGeoPos).
	float s_prevMarioPos[3]{};
	uint16_t s_prevTris = 0;
	std::vector<float> s_prevPosition, s_interpPosition;

	void RecordTickCost(const LARGE_INTEGER& start)
	{
		LARGE_INTEGER end, freq;
		QueryPerformanceCounter(&end);
		QueryPerformanceFrequency(&freq);
		const double ms = 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
		s_tickMsSum += ms;
		s_tickMsWindowMax = ms > s_tickMsWindowMax ? ms : s_tickMsWindowMax;
		if (s_marioTicks % 30 == 0)
		{
			s_tickMsAvg = s_tickMsSum / 30.0;
			s_tickMsMax = s_tickMsWindowMax;
			s_tickMsSum = s_tickMsWindowMax = 0.0;
		}
	}

	const char* ActionName(uint32_t action)
	{
#define ACTION_CASE(name) case ACT_##name: return #name;
		switch (action)
		{
		ACTION_CASE(SPAWN_SPIN_AIRBORNE) ACTION_CASE(SPAWN_SPIN_LANDING) ACTION_CASE(FREEFALL) ACTION_CASE(FREEFALL_LAND)
		ACTION_CASE(IDLE) ACTION_CASE(START_SLEEPING) ACTION_CASE(SLEEPING) ACTION_CASE(WAKING_UP)
		ACTION_CASE(WALKING) ACTION_CASE(BRAKING) ACTION_CASE(DECELERATING) ACTION_CASE(TURNING_AROUND)
		ACTION_CASE(FINISH_TURNING_AROUND) ACTION_CASE(JUMP) ACTION_CASE(DOUBLE_JUMP) ACTION_CASE(TRIPLE_JUMP)
		ACTION_CASE(JUMP_LAND) ACTION_CASE(DOUBLE_JUMP_LAND) ACTION_CASE(TRIPLE_JUMP_LAND) ACTION_CASE(LONG_JUMP)
		ACTION_CASE(BACKFLIP) ACTION_CASE(SIDE_FLIP) ACTION_CASE(DIVE) ACTION_CASE(GROUND_POUND)
		ACTION_CASE(GROUND_POUND_LAND) ACTION_CASE(PUNCHING) ACTION_CASE(MOVE_PUNCHING) ACTION_CASE(JUMP_KICK)
		ACTION_CASE(CROUCHING) ACTION_CASE(START_CROUCHING) ACTION_CASE(STOP_CROUCHING) ACTION_CASE(CROUCH_SLIDE)
		ACTION_CASE(SLIDE_KICK) ACTION_CASE(CRAWLING) ACTION_CASE(STANDING_AGAINST_WALL)
		default: return "?";
		}
#undef ACTION_CASE
	}

	// libsm64 calls are kept free of C++ objects so __try is allowed: a fault is logged instead of crashing the game.
	bool CreateGuarded(const SM64Surface* surfaces, uint32_t count, float x, float y, float z, float faceAngle, int32_t* outId)
	{
		__try
		{
			g_sm64.static_surfaces_load(surfaces, count);
			*outId = g_sm64.mario_create(x, y, z);
			if (*outId >= 0)
				g_sm64.set_mario_faceangle(*outId, faceAngle);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool LoadSurfacesGuarded(const SM64Surface* surfaces, uint32_t count)
	{
		__try
		{
			g_sm64.static_surfaces_load(surfaces, count);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	// Returns -1 on fault, else whether a floor exists below the point.
	int FloorUnderGuarded(const float pos[3])
	{
		__try
		{
			SM64SurfaceCollisionData* floor = nullptr;
			g_sm64.surface_find_floor(pos[0], pos[1], pos[2], &floor);
			return floor != nullptr;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return -1;
		}
	}

	bool TickGuarded(int32_t id, const SM64MarioInputs* inputs, SM64MarioState* state, SM64MarioGeometryBuffers* geometry)
	{
		__try
		{
			g_sm64.mario_tick(id, inputs, state, geometry);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool DeleteGuarded(int32_t id)
	{
		__try
		{
			g_sm64.mario_delete(id);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	const void* PlayerSpace(const PlayerCharacter* player)
	{
		TESObjectCELL* cell = player->parentCell;
		if (!cell)
			return nullptr;
		return cell->worldSpace ? static_cast<const void*>(cell->worldSpace) : static_cast<const void*>(cell);
	}

	void SetFloorTriangle(SM64Surface& s, int32_t y, int32_t x0, int32_t z0, int32_t x1, int32_t z1, int32_t x2, int32_t z2)
	{
		s = {};
		s.type = SURFACE_DEFAULT;
		s.terrain = TERRAIN_STONE;
		const int32_t v[3][2] = { { x0, z0 }, { x1, z1 }, { x2, z2 } };
		for (int i = 0; i < 3; i++)
		{
			s.vertices[i][0] = v[i][0];
			s.vertices[i][1] = y;
			s.vertices[i][2] = v[i][1];
		}
	}

	// Two triangles at height y, centred on (cx, cz), SM64 units. Same winding as SA's loadCollisions (normals up, +Y).
	void AddFloor(std::vector<SM64Surface>& out, int32_t y, int32_t cx, int32_t cz)
	{
		const int32_t w = kFloorHalfWidth;
		const size_t n = out.size();
		out.resize(n + 2);
		SetFloorTriangle(out[n], y, cx + w, cz + w, cx - w, cz - w, cx - w, cz + w);
		SetFloorTriangle(out[n + 1], y, cx - w, cz - w, cx + w, cz + w, cx + w, cz - w);
	}

	// Interiors (M5.4): the cell's statics around `centre` plus the safety floor under them. False if no statics.
	bool BuildInterior(const PlayerCharacter* player, const float centre[3], bool verbose, std::vector<SM64Surface>& out)
	{
		out.clear();
		Collision_AddStatics(player, centre, verbose, out);
		if (out.empty())
			return false;
		int32_t minY = INT32_MAX;
		for (const SM64Surface& surface : out)
			for (int i = 0; i < 3; i++)
				minY = surface.vertices[i][1] < minY ? surface.vertices[i][1] : minY;
		float c[3];
		Units::FnvToSm64(centre[0], centre[1], centre[2], c);
		AddFloor(out, minY - (int32_t)(kSafetyFloorDropFnv / Units::kFnvPerSm64), (int32_t)c[0], (int32_t)c[2]);
		return true;
	}

	// Streaming rebuild around `centre`: terrain + statics outside, statics + safety floor inside. `terrainCount` gets
	// the number of terrain triangles at the front.
	bool BuildStreamed(const PlayerCharacter* player, const float centre[3], std::vector<SM64Surface>& out, uint32_t* terrainCount)
	{
		*terrainCount = 0;
		if (!player->parentCell->worldSpace)
			return BuildInterior(player, centre, false, out);
		if (!Collision_BuildTerrain(player, centre, true, out))
			return false;
		*terrainCount = (uint32_t)out.size();
		Collision_AddStatics(player, centre, false, out);
		return true;
	}

	bool Spawn()
	{
		PlayerCharacter* player = PlayerCharacter::GetSingleton();
		if (!player || !player->parentCell)
		{
			_MESSAGE("mario spawn deferred: player %s", player ? "has no parent cell" : "not available");
			return false;
		}

		float feet[3];
		Units::FnvToSm64(player->posX, player->posY, player->posZ, feet);
		const float centre[3] = { player->posX, player->posY, player->posZ };
		std::vector<SM64Surface>& surfaces = s_surfaces;
		// Exteriors: terrain + statics. Interiors: statics + safety floor. Either falls back to the test floor at the
		// player's feet (plus whatever statics were read) when there's no usable floor under them.
		bool streamed;
		const char* ground;
		if (player->parentCell->worldSpace)
		{
			streamed = Collision_BuildTerrain(player, centre, false, surfaces);
			Collision_AddStatics(player, centre, true, surfaces);
			ground = "terrain + statics";
			float floorY = 0.0f;
			if (streamed && Collision_NearestFloor(surfaces, feet, &floorY))
				_MESSAGE("collision: nearest floor (terrain or static) to the player's feet at %+.1f units",
					(floorY - feet[1]) * Units::kFnvPerSm64);
		}
		else
		{
			Collision_ClearTerrain("interior statics");
			float floorY = 0.0f;
			const bool built = BuildInterior(player, centre, true, surfaces);
			Collision_DumpNearby(centre, "interior spawn");
			const bool found = built && Collision_NearestFloor(surfaces, feet, &floorY);
			const float diff = (floorY - feet[1]) * Units::kFnvPerSm64;
			streamed = found && fabsf(diff) <= kFloorToleranceFnv;
			if (streamed)
				_MESSAGE("collision: interior floor under the player at %+.1f units from their feet", diff);
			else if (found)
				_WARNING("collision: interior: nearest static floor is %+.0f units from the player's feet (tolerance %.0f), test "
					"floor added", diff, kFloorToleranceFnv);
			else
				_WARNING("collision: interior: no static floor under the player (%u triangles), test floor added",
					(uint32_t)surfaces.size());
			ground = "interior statics + safety floor";
		}
		if (!streamed)
		{
			AddFloor(surfaces, (int32_t)feet[1], (int32_t)feet[0], (int32_t)feet[2]);
			ground = "test floor + statics";
		}

		const float spawnY = feet[1] + kSpawnDropFnv / Units::kFnvPerSm64;
		const float faceAngle = Units::HeadingToFaceAngle(player->rotZ);

		if (s_position.empty())
		{
			s_position.resize(9 * SM64_GEO_MAX_TRIANGLES);
			s_normal.resize(9 * SM64_GEO_MAX_TRIANGLES);
			s_color.resize(9 * SM64_GEO_MAX_TRIANGLES);
			s_uv.resize(6 * SM64_GEO_MAX_TRIANGLES);
			s_prevPosition.resize(9 * SM64_GEO_MAX_TRIANGLES);
			s_interpPosition.resize(9 * SM64_GEO_MAX_TRIANGLES);
		}
		s_geometry = { s_position.data(), s_normal.data(), s_color.data(), s_uv.data(), 0 };

		int32_t id = -1;
		Audio_LockLib();
		const bool created = CreateGuarded(surfaces.data(), (uint32_t)surfaces.size(), feet[0], spawnY, feet[2], faceAngle, &id);
		Audio_UnlockLib();
		if (!created)
		{
			SM64_LogFault("mario spawn (surfaces_load/mario_create/set_faceangle)");
			Console_Print("SM64FNV: Mario spawn crashed inside libsm64 (see log)");
			s_spawnFailed = true;
			return false;
		}
		if (id < 0)
		{
			_ERROR("mario spawn: sm64_mario_create returned %d", id);
			Console_Print("SM64FNV: Mario spawn failed (see log)");
			s_spawnFailed = true;
			return false;
		}

		s_marioId = id;
		s_spawnSpace = PlayerSpace(player);
		s_streaming = streamed;
		memcpy(s_collisionCentre, centre, sizeof(s_collisionCentre));
		s_ticksSinceReload = s_reloadRetryDelay = 0;
		s_reloads = s_reloadsFailed = 0;
		s_lastReloadMs = s_maxReloadMs = 0.0;
		s_lastReloadReason = "none";
		s_followFrames = 0;
		s_marioTicks = 0;
		s_tickMsSum = s_tickMsWindowMax = s_tickMsAvg = s_tickMsMax = 0.0;
		s_lastAction = 0;
		s_inputs = {};
		MarioInput_Reset();
		Gameplay_Reset();
		s_state = {};
		_MESSAGE("mario spawned: id %d, player fnv=(%.1f, %.1f, %.1f) heading %.3f cell %08X",
			id, player->posX, player->posY, player->posZ, player->rotZ, player->parentCell->refID);
		_MESSAGE("  %s: %u triangles, streaming %s; mario at sm64=(%.1f, %.1f, %.1f) faceAngle %.3f", ground,
			(uint32_t)surfaces.size(), streamed ? "on" : "off", feet[0], spawnY, feet[2], faceAngle);
		Console_Print("SM64FNV: Mario spawned");
		s_meshDirty = false;
		MarioRender_Create();
		Camera_Acquire();
		Camera_NoteSpawnView();
		Camera_ApplyLoadView();
		if (g_config.input && g_config.blockGameControls)
			ControlFilter_SetActive(true, "spawn");
		return true;
	}

	void SavePrevious()
	{
		memcpy(s_prevMarioPos, s_state.position, sizeof(s_prevMarioPos));
		s_prevTris = s_geometry.numTrianglesUsed;
		memcpy(s_prevPosition.data(), s_position.data(), sizeof(float) * 9 * s_prevTris);
	}

	float Lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	void InterpolatedMarioPos(float alpha, float out[3])
	{
		for (int i = 0; i < 3; i++)
			out[i] = Lerp(s_prevMarioPos[i], s_state.position[i], alpha);
	}

	// libsm64 faulted: its state may be corrupt, so Mario is dropped without calling back into it.
	void DropAfterFault(const char* what)
	{
		SM64_LogFault(what);
		_ERROR("%s fault at tick %u: Mario disabled", what, s_marioTicks);
		Console_Print("SM64FNV: %s crashed inside libsm64 (see log)", what);
		s_marioId = -1;
		CollisionObjects_Clear(false);
		NpcColliders_Clear(false);
		MarioRender_Destroy("libsm64 fault");
		PlayerControl_Release("libsm64 fault");
		PlayerHide_Release("libsm64 fault");
		Camera_Release("libsm64 fault");
		Camera_RestoreView("libsm64 fault");
		ControlFilter_SetActive(false, "libsm64 fault");
		s_spawnFailed = true;
	}

	double MsSince(const LARGE_INTEGER& start)
	{
		LARGE_INTEGER now, freq;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&freq);
		return 1000.0 * (now.QuadPart - start.QuadPart) / freq.QuadPart;
	}

	// Rebuilds terrain + statics around `centre` and swaps them in between two ticks (SA loadCollisions). libsm64
	// re-finds Mario's floor and ceiling at the start of the next tick. On a build failure the old surfaces stay.
	// Returns false if libsm64 faulted (Mario dropped).
	bool ReloadCollision(const float centre[3], float dist, const char* reason)
	{
		s_ticksSinceReload = 0;
		PlayerCharacter* player = PlayerCharacter::GetSingleton();
		if (!player || !player->parentCell || PlayerSpace(player) != s_spawnSpace)
			return true;	// Mario_SyncPlayer handles the space change
		LARGE_INTEGER start;
		QueryPerformanceCounter(&start);
		uint32_t terrainCount = 0;
		if (!BuildStreamed(player, centre, s_surfaces, &terrainCount))
		{
			s_reloadsFailed++;
			s_reloadRetryDelay = kNoFloorCheckTicks;
			_WARNING("collision reload (%s) at fnv (%.0f, %.0f, %.0f) failed: old collision kept", reason, centre[0],
				centre[1], centre[2]);
			return true;
		}
		const double buildMs = MsSince(start);
		if (!LoadSurfacesGuarded(s_surfaces.data(), (uint32_t)s_surfaces.size()))
		{
			DropAfterFault("collision reload (sm64_static_surfaces_load)");
			return false;
		}
		if (!CollisionObjects_Rebuild(!player->parentCell->worldSpace, false))
		{
			DropAfterFault("collision reload (sm64_surface_object_create)");
			return false;
		}
		const double totalMs = MsSince(start);
		s_reloads++;
		memcpy(s_collisionCentre, centre, sizeof(s_collisionCentre));
		s_lastReloadMs = totalMs;
		s_maxReloadMs = totalMs > s_maxReloadMs ? totalMs : s_maxReloadMs;
		s_lastReloadReason = reason;
		_MESSAGE("collision reload #%u (%s, %.0f units from the last centre) at fnv (%.0f, %.0f, %.0f): %u terrain + %u static "
			"triangles; build %.1f ms, load %.1f ms, total %.1f ms", s_reloads, reason, dist, centre[0], centre[1], centre[2],
			terrainCount, (uint32_t)s_surfaces.size() - terrainCount, buildMs, totalMs - buildMs, totalMs);
		return true;
	}

	// After each tick: rebuild when Mario is fReloadDistance from the centre, or has no floor under him (rate-limited).
	bool StreamCollision()
	{
		if (!s_streaming || !g_config.streaming)
			return true;
		s_ticksSinceReload++;
		if (s_reloadRetryDelay)
		{
			s_reloadRetryDelay--;
			return true;
		}
		float pos[3];
		Units::Sm64ToFnv(s_state.position, pos[0], pos[1], pos[2]);
		const float dx = pos[0] - s_collisionCentre[0], dy = pos[1] - s_collisionCentre[1];
		const float dist = sqrtf(dx * dx + dy * dy);
		if (dist > g_config.reloadDistance)
			return ReloadCollision(pos, dist, "moved");
		if (dist > kNoFloorMinMoveFnv && s_ticksSinceReload % kNoFloorCheckTicks == 0)
		{
			const int floor = FloorUnderGuarded(s_state.position);
			if (floor < 0)
			{
				DropAfterFault("sm64_surface_find_floor");
				return false;
			}
			if (!floor)
				return ReloadCollision(pos, dist, "no floor");
		}
		return true;
	}

	void LogState(const char* prefix)
	{
		float fx, fy, fz;
		Units::Sm64ToFnv(s_state.position, fx, fy, fz);
		_MESSAGE("%s t=%u action=0x%08X %s sm64=(%.1f, %.1f, %.1f) fnv=(%.1f, %.1f, %.1f) vy=%.2f health=0x%04X",
			prefix, s_marioTicks, s_state.action, ActionName(s_state.action),
			s_state.position[0], s_state.position[1], s_state.position[2], fx, fy, fz,
			s_state.velocity[1], (uint16_t)s_state.health);
	}
}

void Mario_Tick()
{
	if (!SM64_IsLoaded())
		return;
	if (s_marioId < 0)
	{
		if (s_spawnFailed || !Spawn())
			return;
		PlayerCharacter* player = PlayerCharacter::GetSingleton();
		if (!CollisionObjects_Rebuild(!player->parentCell->worldSpace, true))
		{
			DropAfterFault("mario spawn (sm64_surface_object_create)");
			return;
		}
	}

	if (s_marioTicks > 0)
		SavePrevious();
	if (g_config.input)
		MarioInput_Read(s_inputs);
	// Moving bodies first, so this tick carries Mario with them.
	if (!CollisionObjects_Update())
	{
		DropAfterFault("moving bodies (sm64_surface_object_move/delete)");
		return;
	}
	// NPC colliders around Mario's last position (none before the first tick: s_state is still empty).
	if (s_marioTicks > 0 && !NpcColliders_Update(s_state.position))
	{
		DropAfterFault("npc colliders (sm64_object_create/move/delete)");
		return;
	}
	LARGE_INTEGER tickStart;
	QueryPerformanceCounter(&tickStart);
	if (!TickGuarded(s_marioId, &s_inputs, &s_state, &s_geometry))
	{
		DropAfterFault("mario tick");
		return;
	}
	s_marioTicks++;
	RecordTickCost(tickStart);
	if (!Gameplay_AfterTick(s_marioId, s_state))
	{
		DropAfterFault("gameplay (sm64_set_mario_health/mario_kill/set_mario_action/set_mario_water_level)");
		return;
	}
	if (!NpcColliders_AfterTick(s_marioId, s_state))
	{
		DropAfterFault("npc attacks (sm64_mario_attack)");
		return;
	}
	if (!StreamCollision())
		return;
	s_meshDirty = true;
	// The first tick has nothing to blend from.
	if (s_marioTicks == 1)
		SavePrevious();

	if (s_state.action != s_lastAction)
	{
		s_lastAction = s_state.action;
		LogState("mario action");
	}
	else if (s_marioTicks % kLogEveryTicks == 0)
		LogState("mario");
}

void Mario_SyncPlayer(float alpha)
{
	if (s_marioId < 0 || !g_config.playerFollow)
		return;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player || !player->parentCell)
		return;

	const void* space = PlayerSpace(player);
	if (space != s_spawnSpace)
	{
		// Mario's coordinates belong to the old space; the next tick respawns him at the player.
		_MESSAGE("player changed %s (cell %08X): Mario left behind", player->parentCell->worldSpace ? "worldspace" : "interior",
			player->parentCell->refID);
		Mario_Despawn("player changed space");
		return;
	}

	if (s_followFrames > 0)
	{
		const float dx = player->posX - s_lastFollowPos.x, dy = player->posY - s_lastFollowPos.y, dz = player->posZ - s_lastFollowPos.z;
		const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
		if (dist > kTeleportDistFnv)
		{
			_MESSAGE("player teleported %.0f units (to %.1f, %.1f, %.1f): Mario left behind", dist, player->posX, player->posY, player->posZ);
			Mario_Despawn("player teleported");
			return;
		}
	}

	float marioPos[3];
	InterpolatedMarioPos(g_config.interpolate ? alpha : 1.0f, marioPos);
	NiPoint3 pos;
	Units::Sm64ToFnv(marioPos, pos.x, pos.y, pos.z);
	if (s_followFrames == 0)
	{
		_MESSAGE("player follow start: player fnv=(%.1f, %.1f, %.1f) -> mario fnv=(%.1f, %.1f, %.1f)",
			player->posX, player->posY, player->posZ, pos.x, pos.y, pos.z);
		if (g_config.disableControls)
			PlayerControl_Acquire();
	}
	ThisStdCall<void>(GameAddr::kMobileObjectSetPosition, player, &pos);
	s_lastFollowPos = pos;
	s_followFrames++;
}

void Mario_UpdateRender(float alpha)
{
	if (s_marioId < 0)
		return;
	if (!g_config.interpolate)
	{
		// The latest tick's mesh, uploaded only when it changed.
		if (!s_meshDirty)
			return;
		s_meshDirty = false;
		MarioRender_Update(s_geometry, s_state.position);
		return;
	}

	// Every frame: each vertex blended between the last two ticks, as SA's marioRenderInterpolate. Vertices the
	// previous tick didn't have (triangle count changed) take the latest position.
	s_meshDirty = false;
	const uint32_t blended = 9 * (s_prevTris < s_geometry.numTrianglesUsed ? s_prevTris : s_geometry.numTrianglesUsed);
	const uint32_t total = 9 * s_geometry.numTrianglesUsed;
	for (uint32_t i = 0; i < blended; i++)
		s_interpPosition[i] = Lerp(s_prevPosition[i], s_position[i], alpha);
	for (uint32_t i = blended; i < total; i++)
		s_interpPosition[i] = s_position[i];
	SM64MarioGeometryBuffers interp = s_geometry;
	interp.position = s_interpPosition.data();
	float marioPos[3];
	InterpolatedMarioPos(alpha, marioPos);
	MarioRender_Update(interp, marioPos);
}

void Mario_Despawn(const char* reason)
{
	if (s_marioId < 0)
		return;
	CollisionObjects_Clear(true);
	NpcColliders_Clear(true);
	// The audio thread mixes sounds that point into Mario's objects.
	Audio_LockLib();
	const bool deleted = DeleteGuarded(s_marioId);
	Audio_UnlockLib();
	if (!deleted)
		SM64_LogFault("mario despawn");
	_MESSAGE("mario despawned (%s) after %u ticks, %u player follow frames", reason, s_marioTicks, s_followFrames);
	Console_Print("SM64FNV: Mario despawned");
	MarioRender_Destroy(reason);
	PlayerControl_Release(reason);
	PlayerHide_Release(reason);
	Camera_Release(reason);
	ControlFilter_SetActive(false, reason);
	s_marioId = -1;
	s_spawnSpace = nullptr;
}

bool Mario_IsSpawned()
{
	return s_marioId >= 0;
}

void Mario_ResetFailure()
{
	s_spawnFailed = false;
}

void Mario_DumpState()
{
	if (s_marioId < 0)
	{
		Console_Print("SM64FNV: Mario not spawned%s", s_spawnFailed ? " (spawn failed)" : "");
		return;
	}
	float fx, fy, fz;
	Units::Sm64ToFnv(s_state.position, fx, fy, fz);
	Console_Print("SM64FNV: Mario t=%u action=0x%08X %s fnv=(%.0f, %.0f, %.0f)",
		s_marioTicks, s_state.action, ActionName(s_state.action), fx, fy, fz);
	if (PlayerCharacter* player = PlayerCharacter::GetSingleton())
		Console_Print("SM64FNV: player fnv=(%.0f, %.0f, %.0f)", player->posX, player->posY, player->posZ);
	Console_Print("SM64FNV: player follow %s, %u frames; interpolation %s", g_config.playerFollow ? "on" : "off (ini)",
		s_followFrames, g_config.interpolate ? "on" : "off (ini)");
	LogState("state dump mario");
	if (g_config.input)
		MarioInput_DumpState();
	Collision_DumpState();
	Collision_DumpStatics();
	CollisionObjects_Dump();
	NpcColliders_Dump();
	{
		float feet[3];
		Units::Sm64ToFnv(s_state.position, feet[0], feet[1], feet[2]);
		Collision_DumpNearby(feet, "F10");
	}
	Console_Print("SM64FNV: sm64_mario_tick avg %.2f ms, max %.2f ms (last 30 ticks)", s_tickMsAvg, s_tickMsMax);
	{
		float fx, fy, fz;
		Units::Sm64ToFnv(s_state.position, fx, fy, fz);
		const float dist = sqrtf((fx - s_collisionCentre[0]) * (fx - s_collisionCentre[0]) +
			(fy - s_collisionCentre[1]) * (fy - s_collisionCentre[1]));
		Console_Print("SM64FNV: streaming %s: %u reloads (%u failed), last %.0f ms (%s), max %.0f ms; Mario %.0f units from the centre",
			!s_streaming ? "off (test floor)" : g_config.streaming ? "on" : "off (ini)", s_reloads, s_reloadsFailed,
			s_lastReloadMs, s_lastReloadReason, s_maxReloadMs, dist);
		_MESSAGE("state dump streaming %s: %u reloads (%u failed), last %.1f ms (%s), max %.1f ms, centre fnv (%.0f, %.0f), "
			"Mario %.0f units away", !s_streaming ? "off (test floor)" : g_config.streaming ? "on" : "off (ini)", s_reloads,
			s_reloadsFailed, s_lastReloadMs, s_lastReloadReason, s_maxReloadMs, s_collisionCentre[0], s_collisionCentre[1], dist);
	}
	_MESSAGE("state dump tick cost: avg %.3f ms, max %.3f ms over the last 30 ticks", s_tickMsAvg, s_tickMsMax);
	Gameplay_DumpState();
	Camera_DumpState();
	Camera_DumpView();
	MarioRender_DumpState();
}
