#include "collision_objects.h"
#include "collision.h"
#include "config.h"
#include "game_addrs.h"
#include "sm64_loader.h"
#include "units.h"
#include "nvse/GameAPI.h"
#include "nvse/GameForms.h"
#include "nvse/Utilities.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace
{
	using namespace GameAddr;

	constexpr uint32_t kLoggedObjects = 12;
	constexpr uint32_t kLoggedMotionLines = 40;
	// Pose changes below these count as still (SM64 units, degrees).
	constexpr float kMoveEpsilon = 0.05f, kTurnEpsilon = 0.01f;

	struct Object
	{
		MovingBody src;
		uint32_t id;
		uint32_t triangles;
		SM64ObjectTransform xf;		// last transform given to libsm64
		SM64ObjectTransform motionStart;
		bool moving;				// changed at the last update: libsm64 must get one more move to zero its velocity
		uint32_t motionTicks, moves;
	};

	std::vector<Object> s_objects;
	std::vector<SM64Surface> s_scratch;
	struct BuildStats
	{
		uint32_t found, created, overCount, empty, invalid, tooSmall, triangles, capped;
	} s_build{};
	uint32_t s_rebuilds = 0, s_lost = 0, s_motionLines = 0, s_movesTotal = 0;

	bool CreateGuarded(const SM64SurfaceObject* object, uint32_t* id)
	{
		__try
		{
			*id = g_sm64.surface_object_create(object);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool MoveGuarded(uint32_t id, const SM64ObjectTransform* xf)
	{
		__try
		{
			g_sm64.surface_object_move(id, xf);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool DeleteGuarded(uint32_t id)
	{
		__try
		{
			g_sm64.surface_object_delete(id);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	volatile LONG& RefCount(void* object)
	{
		return *reinterpret_cast<volatile LONG*>(reinterpret_cast<uint8_t*>(object) + kRefCount);
	}

	// Our reference keeps the bhkRigidBody (and the hkpRigidBody it owns) allocated after its cell lets go of it.
	void Release(void* worldObject)
	{
		if (InterlockedDecrement(&RefCount(worldObject)) == 0)
			ThisStdCall<void>(*reinterpret_cast<UInt32*>(*reinterpret_cast<UInt32*>(worldObject) + kVtblDeleteThis), worldObject);
	}

	void ToTransform(const BodyPose& pose, SM64ObjectTransform* xf)
	{
		Units::FnvToSm64(pose.pos[0], pose.pos[1], pose.pos[2], xf->position);
		Units::RotationToSm64Euler(pose.rot, xf->eulerRotation);
	}

	float AngleDiff(float a, float b)
	{
		float d = fmodf(a - b, 360.0f);
		if (d > 180.0f)
			d -= 360.0f;
		else if (d < -180.0f)
			d += 360.0f;
		return d;
	}

	bool Changed(const SM64ObjectTransform& a, const SM64ObjectTransform& b)
	{
		for (int k = 0; k < 3; k++)
			if (fabsf(a.position[k] - b.position[k]) > kMoveEpsilon || fabsf(AngleDiff(a.eulerRotation[k], b.eulerRotation[k])) > kTurnEpsilon)
				return true;
		return false;
	}

	const char* Kind(const Object& o)
	{
		return o.src.door ? "door" : o.src.dynamic ? "loose object" : "body";
	}

	// Largest side of the surfaces' local box, game units.
	float LargestSide(const std::vector<SM64Surface>& surfaces)
	{
		int32_t lo[3] = { INT32_MAX, INT32_MAX, INT32_MAX }, hi[3] = { INT32_MIN, INT32_MIN, INT32_MIN };
		for (const SM64Surface& surface : surfaces)
			for (const auto& v : surface.vertices)
				for (int k = 0; k < 3; k++)
				{
					lo[k] = v[k] < lo[k] ? v[k] : lo[k];
					hi[k] = v[k] > hi[k] ? v[k] : hi[k];
				}
		int32_t side = 0;
		for (int k = 0; k < 3; k++)
			side = hi[k] - lo[k] > side ? hi[k] - lo[k] : side;
		return side * Units::kFnvPerSm64;
	}

	void LogMotion(const Object& o, bool started)
	{
		if (s_motionLines >= kLoggedMotionLines)
			return;
		s_motionLines++;
		float fnv[3];
		Units::Sm64ToFnv(o.xf.position, fnv[0], fnv[1], fnv[2]);
		if (started)
		{
			_MESSAGE("moving %s ref %08X starts moving at fnv (%.0f, %.0f, %.0f)", Kind(o), o.src.refID, fnv[0], fnv[1], fnv[2]);
			return;
		}
		float d = 0.0f;
		for (int k = 0; k < 3; k++)
			d += (o.xf.position[k] - o.motionStart.position[k]) * (o.xf.position[k] - o.motionStart.position[k]);
		_MESSAGE("moving %s ref %08X stopped after %u ticks: moved %.1f units, turned pitch %+.1f yaw %+.1f roll %+.1f deg, now at "
			"fnv (%.0f, %.0f, %.0f)", Kind(o), o.src.refID, o.motionTicks, sqrtf(d) * Units::kFnvPerSm64,
			AngleDiff(o.xf.eulerRotation[0], o.motionStart.eulerRotation[0]), AngleDiff(o.xf.eulerRotation[1], o.motionStart.eulerRotation[1]),
			AngleDiff(o.xf.eulerRotation[2], o.motionStart.eulerRotation[2]), fnv[0], fnv[1], fnv[2]);
	}
}

bool CollisionObjects_Rebuild(bool interior, bool verbose)
{
	CollisionObjects_Clear(true);
	s_build = {};
	if (!g_config.movingBodies)
		return true;
	std::vector<MovingBody> bodies = Collision_MovingBodies();
	std::sort(bodies.begin(), bodies.end(), [](const MovingBody& a, const MovingBody& b) { return a.dist < b.dist; });
	s_build.found = (uint32_t)bodies.size();
	s_rebuilds++;
	for (const MovingBody& b : bodies)
	{
		if (s_objects.size() >= g_config.maxMovingBodies)
		{
			s_build.overCount++;
			continue;
		}
		BodyPose pose;
		if (!Collision_BodySurfaces(b, interior, g_config.maxMovingTriangles, s_scratch, &pose))
		{
			s_build.invalid++;
			continue;
		}
		if (s_scratch.empty())
		{
			s_build.empty++;
			continue;
		}
		// M8.2: cans, bottles and other small loose items would only trip Mario.
		if (b.dynamic && LargestSide(s_scratch) < g_config.minDynamicSize)
		{
			s_build.tooSmall++;
			continue;
		}
		s_build.capped += s_scratch.size() >= g_config.maxMovingTriangles;
		Object o{};
		o.src = b;
		o.triangles = (uint32_t)s_scratch.size();
		ToTransform(pose, &o.xf);
		SM64SurfaceObject object{};
		object.transform = o.xf;
		object.surfaceCount = o.triangles;
		object.surfaces = s_scratch.data();
		InterlockedIncrement(&RefCount(b.worldObject));
		if (!CreateGuarded(&object, &o.id))
		{
			Release(b.worldObject);
			SM64_LogFault("sm64_surface_object_create");
			return false;
		}
		s_objects.push_back(o);
		s_build.created++;
		s_build.triangles += o.triangles;
		if (verbose && s_objects.size() <= kLoggedObjects)
		{
			float fnv[3];
			Units::Sm64ToFnv(o.xf.position, fnv[0], fnv[1], fnv[2]);
			_MESSAGE("  moving %s ref %08X base %08X (form type %u), layer %u, %u triangles, %.0f units away, fnv (%.0f, %.0f, %.0f), "
				"euler (%.1f, %.1f, %.1f), libsm64 id %u", Kind(o), b.refID, b.baseID, b.baseType, b.layer, o.triangles, b.dist, fnv[0],
				fnv[1], fnv[2], o.xf.eulerRotation[0], o.xf.eulerRotation[1], o.xf.eulerRotation[2], o.id);
		}
	}
	uint32_t doors = 0, loose = 0;
	for (const Object& o : s_objects)
	{
		doors += o.src.door;
		loose += o.src.dynamic;
	}
	_MESSAGE("collision: moving bodies: %u found, %u objects created (%u doors, %u loose objects), %u triangles; %u smaller than "
		"fMinDynamicSize, %u over iMaxMovingBodies, %u empty (unhandled shapes), %u invalid, %u hit iMaxMovingTriangles", s_build.found,
		s_build.created, doors, loose, s_build.triangles, s_build.tooSmall, s_build.overCount, s_build.empty, s_build.invalid,
		s_build.capped);
	return true;
}

bool CollisionObjects_Update()
{
	for (size_t i = 0; i < s_objects.size();)
	{
		Object& o = s_objects[i];
		BodyPose pose;
		if (!Collision_ReadBodyPose(o.src, &pose))
		{
			// Unloaded, disabled or replaced: its last pose would be stale collision.
			_MESSAGE("moving %s ref %08X: body gone from the Havok world, object removed", Kind(o), o.src.refID);
			const bool ok = DeleteGuarded(o.id);
			Release(o.src.worldObject);
			s_objects.erase(s_objects.begin() + i);
			s_lost++;
			if (!ok)
			{
				SM64_LogFault("sm64_surface_object_delete");
				return false;
			}
			continue;
		}
		SM64ObjectTransform xf;
		ToTransform(pose, &xf);
		const bool changed = Changed(xf, o.xf);
		// A move also sets the platform velocity libsm64 carries Mario with, so a body that just stopped gets one more.
		if (changed || o.moving)
		{
			if (!MoveGuarded(o.id, &xf))
			{
				SM64_LogFault("sm64_surface_object_move");
				return false;
			}
			if (changed && !o.moving)
			{
				o.motionStart = o.xf;
				o.motionTicks = 0;
			}
			o.xf = xf;
			o.moves++;
			s_movesTotal++;
			if (changed)
				o.motionTicks++;
			if (changed != o.moving)
				LogMotion(o, changed);
			o.moving = changed;
		}
		i++;
	}
	return true;
}

void CollisionObjects_Clear(bool callLibsm64)
{
	bool lib = callLibsm64;
	for (const Object& o : s_objects)
	{
		if (lib && !DeleteGuarded(o.id))
		{
			SM64_LogFault("sm64_surface_object_delete");
			lib = false;
		}
		Release(o.src.worldObject);
	}
	s_objects.clear();
}

void CollisionObjects_Abandon()
{
	s_objects.clear();
}

void CollisionObjects_Dump()
{
	uint32_t moving = 0, doors = 0, loose = 0, triangles = 0;
	for (const Object& o : s_objects)
	{
		moving += o.moving;
		doors += o.src.door;
		loose += o.src.dynamic;
		triangles += o.triangles;
	}
	if (!g_config.movingBodies)
		Console_Print("SM64FNV: moving bodies off (ini)");
	else
		Console_Print("SM64FNV: moving bodies: %u objects (%u doors, %u loose, %u triangles), %u moving now, %u moves, %u lost; last "
			"build found %u (%u too small)", (uint32_t)s_objects.size(), doors, loose, triangles, moving, s_movesTotal, s_lost,
			s_build.found, s_build.tooSmall);
	_MESSAGE("state dump moving bodies: %u objects (%u doors, %u loose, %u triangles), %u moving now, %u moves total, %u lost, %u "
		"rebuilds; last build found %u, %u too small, %u over cap, %u empty, %u invalid", (uint32_t)s_objects.size(), doors, loose,
		triangles, moving, s_movesTotal, s_lost, s_rebuilds, s_build.found, s_build.tooSmall, s_build.overCount, s_build.empty,
		s_build.invalid);
	for (const Object& o : s_objects)
	{
		if (!o.moves)
			continue;
		float fnv[3];
		Units::Sm64ToFnv(o.xf.position, fnv[0], fnv[1], fnv[2]);
		_MESSAGE("  moving %s ref %08X base %08X: %u moves, fnv (%.0f, %.0f, %.0f), euler (%.1f, %.1f, %.1f)%s", Kind(o), o.src.refID,
			o.src.baseID, o.moves, fnv[0], fnv[1], fnv[2], o.xf.eulerRotation[0], o.xf.eulerRotation[1], o.xf.eulerRotation[2],
			o.moving ? ", moving" : "");
	}
}
