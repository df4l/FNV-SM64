#include "npc_colliders.h"
#include "config.h"
#include "game_addrs.h"
#include "sm64_loader.h"
#include "units.h"
#include "nvse/GameAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/GameProcess.h"
#include "nvse/GameSettings.h"
#include "nvse/Utilities.h"
#include "decomp/include/sm64shared.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace
{
	using namespace GameAddr;

	constexpr uint32_t kMaxLogLines = 40;
	constexpr uint32_t kMaxCandidates = 256;
	constexpr uint32_t kMaxListNodes = 4096;	// guards the list walk against a broken link
	// Actor lifeState (NVSE GameObjects.h): 0 alive, 5 restrained (standing). Dying, dead, unconscious get no collider.
	constexpr UInt32 kLifeAlive = 0, kLifeRestrained = 5;
	// Mario's hitbox (fork object_stuff.c: radius 37, height 160), only to count contacts for the log.
	constexpr float kMarioRadius = 37.0f, kMarioHeight = 160.0f, kContactSlack = 2.0f;
	constexpr float kMoveEpsilon = 0.01f;	// SM64 units
	// Sanity limits on bounds-derived sizes, game units before the ref scale.
	constexpr float kMinBoundsRadius = 6.0f, kMaxBoundsRadius = 200.0f;
	constexpr SInt16 kMinBoundsHeight = 8;
	constexpr float kMaxBoundsHeight = 600.0f;

	struct ListNode
	{
		Actor* data;
		ListNode* next;
	};

	struct Candidate
	{
		UInt32 refID, baseID;
		UInt8 baseType;
		float pos[3];	// game units, feet
		float scale;
		float dist2;
		bool hasBounds;
		SInt16 bounds[6];	// base form OBND, game units: x1 y1 z1 x2 y2 z2
	};

	struct Collider
	{
		UInt32 refID, baseID;
		UInt8 baseType;
		uint32_t id;
		float pos[3];	// SM64 units, last given to libsm64
		float radius, height;	// SM64 units
		bool touching;
		uint32_t contacts;
		uint32_t hits, lastHitTick;	// M10.4 damaging hits, attack tick of the last one
		uint32_t lastContactTick;	// attack tick of the last sm64_mario_attack contact (0 = none)
	};

	std::vector<Collider> s_colliders;
	Candidate s_candidates[kMaxCandidates];
	uint32_t s_logLines = 0;
	uint32_t s_seen = 0, s_inRange = 0, s_overflow = 0;
	uint32_t s_created = 0, s_deleted = 0, s_contacts = 0, s_moves = 0;
	float s_nearest = -1.0f;	// game units, last update
	UInt32 s_nearestRef = 0;
	bool s_gameFault = false;	// the list walk faulted: colliders off until the next spawn

	void LogLine(const char* fmt, ...)
	{
		if (s_logLines >= kMaxLogLines)
			return;
		if (++s_logLines == kMaxLogLines)
			_MESSAGE("npc: log cap reached (%u lines), F10 still reports", kMaxLogLines);
		char buffer[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		_MESSAGE("%s", buffer);
	}

	bool SameSpace(const TESObjectCELL* a, const TESObjectCELL* b)
	{
		if (!a || !b)
			return false;
		return a == b || (a->worldSpace && a->worldSpace == b->worldSpace);
	}

	// Walks ProcessLists::highActors (gameplay.md §4): living, loaded actors other than the player, in the player's
	// cell (interior) or worldspace, within range of `centre`. False if a read faulted.
	bool ScanGuarded(PlayerCharacter* player, const float centre[3], float range2, uint32_t* count, uint32_t* seen,
		uint32_t* overflow)
	{
		__try
		{
			*count = *seen = *overflow = 0;
			const ListNode* node = reinterpret_cast<const ListNode*>(kProcessLists + kProcessListsHighActors);
			for (uint32_t i = 0; node && i < kMaxListNodes; node = node->next, i++)
			{
				Actor* actor = node->data;
				if (!actor || actor == player)
					continue;
				(*seen)++;
				if (actor->lifeState != kLifeAlive && actor->lifeState != kLifeRestrained)
					continue;
				if (actor->IsDeleted() || !actor->baseForm || !SameSpace(actor->parentCell, player->parentCell))
					continue;
				const float dx = actor->posX - centre[0], dy = actor->posY - centre[1], dz = actor->posZ - centre[2];
				const float d2 = dx * dx + dy * dy + dz * dz;
				if (d2 > range2 || !actor->GetNiNode())
					continue;
				if (*count >= kMaxCandidates)
				{
					(*overflow)++;
					continue;
				}
				Candidate& c = s_candidates[(*count)++];
				c.refID = actor->refID;
				c.baseID = actor->baseForm->refID;
				c.baseType = actor->baseForm->typeID;
				c.pos[0] = actor->posX;
				c.pos[1] = actor->posY;
				c.pos[2] = actor->posZ;
				c.scale = actor->scale > 0.0f ? actor->scale : 1.0f;
				c.dist2 = d2;
				c.hasBounds = c.baseType == kFormType_TESNPC || c.baseType == kFormType_TESCreature;
				if (c.hasBounds)
					memcpy(c.bounds, static_cast<TESBoundObject*>(actor->baseForm)->bounds, sizeof(c.bounds));
			}
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool CreateGuarded(SM64ObjectCollider* collider, uint32_t* id)
	{
		__try
		{
			*id = g_sm64.object_create(collider);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool MoveGuarded(uint32_t id, const float pos[3])
	{
		__try
		{
			g_sm64.object_move(id, pos[0], pos[1], pos[2]);
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
			g_sm64.object_delete(id);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool DeleteAll()
	{
		bool ok = true;
		for (const Collider& c : s_colliders)
		{
			if (ok && !DeleteGuarded(c.id))
				ok = false;
			s_deleted++;
		}
		s_colliders.clear();
		return ok;
	}

	// SA's overlap test (fork detect_object_hitbox_overlap) with a little slack: Mario resolved against a collider
	// ends up exactly at the radius sum.
	bool Touching(const Collider& c, const float mario[3])
	{
		const float dx = mario[0] - c.pos[0], dz = mario[2] - c.pos[2];
		const float reach = c.radius + kMarioRadius + kContactSlack;
		return dx * dx + dz * dz <= reach * reach && mario[1] <= c.pos[1] + c.height && mario[1] + kMarioHeight >= c.pos[1];
	}

	// --- M10.4 attacks (SA mario.cpp ped loop, gameplay.md §3) ---
	enum HitResult { kHitDamaged, kHitKilled, kHitGone, kHitInvulnerable };
	uint32_t s_attackTick = 0;
	uint32_t s_attackLogLines = 0;
	uint32_t s_attackTests = 0, s_attackContacts = 0, s_attackHits = 0, s_attackCooled = 0, s_attackKills = 0;
	uint32_t s_attackKnocks = 0, s_attackGone = 0;
	float s_attackDamageDealt = 0.0f;
	bool s_attackFault = false;	// a game call faulted: attacks off until the next spawn
	bool s_unarmedFault = false;	// the unarmed damage call faulted: fAttackDamage until the next spawn

	void AttackLog(const char* fmt, ...)
	{
		if (s_attackLogLines >= kMaxLogLines)
			return;
		if (++s_attackLogLines == kMaxLogLines)
			_MESSAGE("attack: log cap reached (%u lines), F10 still reports", kMaxLogLines);
		char buffer[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		_MESSAGE("%s", buffer);
	}

	bool AttackGuarded(int32_t marioId, float x, float y, float z, bool* hit)
	{
		__try
		{
			*hit = g_sm64.mario_attack(marioId, x, y, z, 0.0f);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool ForwardVelocityGuarded(int32_t marioId, float velocity)
	{
		__try
		{
			g_sm64.set_mario_forward_velocity(marioId, velocity);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	// Vanilla armour (JIP's CalculateHitDamage port of 0x009B5A30): DR scales damage and DT, then DT is subtracted,
	// with fMinDamMultiplier of the raw damage as the floor. No crits, sneak or perk entry points.
	float s_maxArmorRating = -1.0f, s_minDamMult = 0.2f;

	void LoadArmorSettings()
	{
		double value;
		s_maxArmorRating = GetNumericGameSetting("fMaxArmorRating", &value) ? (float)value : 85.0f;
		s_minDamMult = GetNumericGameSetting("fMinDamMultiplier", &value) ? (float)value : 0.2f;
		_MESSAGE("attack: armour settings fMaxArmorRating %.1f, fMinDamMultiplier %.2f", s_maxArmorRating, s_minDamMult);
	}

	float ApplyArmor(float raw, float dr, float dt)
	{
		dr = (std::min)(dr, 100.0f);
		const float resist = dr <= 0.0f ? 1.0f : 1.0f - (std::min)(dr, s_maxArmorRating) * 0.01f;
		const float damage = raw * resist - (std::max)(dt, 0.0f) * resist;
		return (std::max)(damage, s_minDamMult * raw);
	}

	// The player's bare-hand hit damage from the engine's weapon damage function with no weapon (base 1 x Unarmed
	// skill scale + UnarmedDamage AV, condition 100 %), called as JIP's CalculateWeaponDamage does.
	bool UnarmedDamageGuarded(PlayerCharacter* player, float* damage)
	{
		__try
		{
			*damage = CdeclCall<float>(kCalcWeaponDamage, &player->avOwner, static_cast<TESObjectWEAP*>(nullptr), 1.0f, 1.0f,
				0, 0, static_cast<void*>(nullptr), 0);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	// The ref is looked up again (it may have unloaded since the collider pass). Armour (vanilla mode), DoDamage with
	// the player as attacker, then KnockExplosion from Mario's position as Cmd_PushActorAway does. False on a fault.
	bool HitActorGuarded(UInt32 refID, float raw, bool armor, bool knock, const float source[3], HitResult* result,
		float* damage, float* health, float* dr, float* dt, bool* knocked)
	{
		__try
		{
			*knocked = false;
			*health = *dr = *dt = 0.0f;
			*damage = raw;
			TESForm* form = LookupFormByID(refID);
			if (!form || (form->typeID != kFormType_Character && form->typeID != kFormType_Creature))
			{
				*result = kHitGone;
				return true;
			}
			Actor* actor = static_cast<Actor*>(form);
			if (actor->IsDeleted() || (actor->lifeState != kLifeAlive && actor->lifeState != kLifeRestrained))
			{
				*result = kHitGone;
				return true;
			}
			if (armor)
			{
				*dr = actor->avOwner.Fn_03(kAVDamageResist);
				*dt = actor->avOwner.Fn_03(kAVDamageThreshold);
				*damage = ApplyArmor(raw, *dr, *dt);
			}
			const float before = actor->avOwner.Fn_03(kAVHealth);
			const UInt32 doDamage = (*reinterpret_cast<UInt32**>(actor))[kVtblActorDoDamage / 4];
			const bool dead = ThisStdCall<bool>(doDamage, actor, *damage, 0.0f,
				static_cast<Actor*>(PlayerCharacter::GetSingleton()));
			*health = actor->avOwner.Fn_03(kAVHealth);
			*result = dead ? kHitKilled : *health < before ? kHitDamaged : kHitInvulnerable;
			BaseProcess* process = actor->baseProcess;
			if (knock && !dead && g_config.knockForce > 0 && process && process->processLevel == 0)
			{
				const float force = CdeclCall<float>(kAdjustPushForce, &actor->avOwner, g_config.knockForce);
				const UInt32 knockFn = (*reinterpret_cast<UInt32**>(process))[kVtblProcessKnockExplosion / 4];
				ThisStdCall<void>(knockFn, process, actor, source[0], source[1], source[2], force);
				*knocked = true;
			}
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	const char* AttackName(uint32_t action, bool air)
	{
		switch (action)
		{
		case ACT_PUNCHING: case ACT_MOVE_PUNCHING: return "punch";
		case ACT_JUMP_KICK: return "jump kick";
		case ACT_SLIDE_KICK: case ACT_SLIDE_KICK_SLIDE: return "slide kick";
		case ACT_GROUND_POUND: case ACT_GROUND_POUND_LAND: return "ground pound";
		}
		if (action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE)
			return "slide";
		if (action & ACT_FLAG_ATTACKING)
			return "fast attack";
		return air ? "stomp" : "other";
	}
}

bool NpcColliders_Update(const float marioPos[3])
{
	if (!g_config.npcColliders || s_gameFault)
		return true;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player || !player->parentCell)
		return true;

	float centre[3];
	Units::Sm64ToFnv(marioPos, centre[0], centre[1], centre[2]);
	uint32_t count;
	if (!ScanGuarded(player, centre, g_config.npcRange * g_config.npcRange, &count, &s_seen, &s_overflow))
	{
		SM64_LogFault("npc scan (ProcessLists high actors)");
		_ERROR("npc: walking the high-process actor list faulted, NPC colliders off until the next spawn");
		Console_Print("SM64FNV: NPC colliders off after a fault (see log)");
		s_gameFault = true;
		return DeleteAll();
	}
	s_inRange = count;
	std::sort(s_candidates, s_candidates + count, [](const Candidate& a, const Candidate& b) { return a.dist2 < b.dist2; });
	const uint32_t kept = (std::min)(count, g_config.maxNpcColliders);
	s_nearest = count ? sqrtf(s_candidates[0].dist2) : -1.0f;
	s_nearestRef = count ? s_candidates[0].refID : 0;

	// Colliders whose actor left the kept set (out of range, beyond the limit, dead, unloaded).
	for (size_t i = 0; i < s_colliders.size();)
	{
		const Collider& c = s_colliders[i];
		const Candidate* match = std::find_if(s_candidates, s_candidates + kept, [&](const Candidate& k) { return k.refID == c.refID; });
		if (match != s_candidates + kept)
		{
			i++;
			continue;
		}
		const Candidate* any = std::find_if(s_candidates, s_candidates + count, [&](const Candidate& k) { return k.refID == c.refID; });
		LogLine("npc: collider %u removed from ref %08X (%s, %u contacts)", c.id, c.refID,
			any != s_candidates + count ? "beyond iMaxNpcColliders" : "dead, gone or out of range", c.contacts);
		s_deleted++;
		if (!DeleteGuarded(c.id))
		{
			s_colliders.erase(s_colliders.begin() + i);
			return false;
		}
		s_colliders.erase(s_colliders.begin() + i);
	}

	for (uint32_t k = 0; k < kept; k++)
	{
		const Candidate& cand = s_candidates[k];
		float pos[3];
		Units::FnvToSm64(cand.pos[0], cand.pos[1], cand.pos[2], pos);
		auto it = std::find_if(s_colliders.begin(), s_colliders.end(), [&](const Collider& c) { return c.refID == cand.refID; });
		if (it == s_colliders.end())
		{
			// Size: base form bounds (mean of the two horizontal half extents, top above the feet), else the ini size.
			float radius = g_config.npcRadius, height = g_config.npcHeight;
			const SInt16* b = cand.bounds;
			const bool fromBounds = g_config.npcBounds && cand.hasBounds && b[3] > b[0] && b[4] > b[1] &&
				b[5] >= kMinBoundsHeight;
			if (fromBounds)
			{
				radius = (std::min)(((b[3] - b[0]) + (b[4] - b[1])) * 0.25f * g_config.npcBoundsScale, kMaxBoundsRadius);
				radius = (std::max)(radius, kMinBoundsRadius);
				height = (std::min)((float)b[5], kMaxBoundsHeight);
			}
			SM64ObjectCollider desc;
			memcpy(desc.position, pos, sizeof(pos));
			desc.radius = radius * cand.scale / Units::kFnvPerSm64;
			desc.height = height * cand.scale / Units::kFnvPerSm64;
			uint32_t id;
			if (!CreateGuarded(&desc, &id))
				return false;
			s_created++;
			s_colliders.push_back({ cand.refID, cand.baseID, cand.baseType, id, { pos[0], pos[1], pos[2] }, desc.radius,
				desc.height, false, 0, 0, 0, 0 });
			LogLine("npc: collider %u on ref %08X (base %08X, form type %u, scale %.2f) at %.0f units, fnv=(%.1f, %.1f, %.1f), "
				"sm64 radius %.1f height %.1f from %s (bounds %d %d %d / %d %d %d)", id, cand.refID, cand.baseID, cand.baseType,
				cand.scale, sqrtf(cand.dist2), cand.pos[0], cand.pos[1], cand.pos[2], desc.radius, desc.height,
				fromBounds ? "bounds" : "ini", cand.hasBounds ? b[0] : 0, cand.hasBounds ? b[1] : 0, cand.hasBounds ? b[2] : 0,
				cand.hasBounds ? b[3] : 0, cand.hasBounds ? b[4] : 0, cand.hasBounds ? b[5] : 0);
			continue;
		}
		if (fabsf(it->pos[0] - pos[0]) > kMoveEpsilon || fabsf(it->pos[1] - pos[1]) > kMoveEpsilon ||
			fabsf(it->pos[2] - pos[2]) > kMoveEpsilon)
		{
			if (!MoveGuarded(it->id, pos))
				return false;
			memcpy(it->pos, pos, sizeof(pos));
			s_moves++;
		}
	}

	for (Collider& c : s_colliders)
	{
		const bool touching = Touching(c, marioPos);
		if (touching && !c.touching)
		{
			c.contacts++;
			s_contacts++;
			LogLine("npc: Mario touches ref %08X (collider %u, contact %u)", c.refID, c.id, c.contacts);
		}
		c.touching = touching;
	}
	return true;
}

bool NpcColliders_AfterTick(int32_t marioId, const SM64MarioState& state)
{
	s_attackTick++;
	if (!g_config.npcColliders || !g_config.npcAttacks || s_gameFault || s_attackFault || s_colliders.empty())
		return true;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player || player->lifeState != kLifeAlive)
		return true;

	// SA tests 1.25 m (2D, ground) / 1 m (3D, air) from the ped's centre; here the reach is measured from the
	// collider's edge so big creatures are hit from as far as small ones. sm64_mario_attack then decides from Mario's
	// action, facing and vertical speed (fork fake_interact_bounce_top).
	const bool air = (state.action & ACT_FLAG_AIR) != 0;
	const float reach = (air ? g_config.attackReachAir : g_config.attackReach) / Units::kFnvPerSm64;
	for (Collider& c : s_colliders)
	{
		const float middle = c.pos[1] + c.height * 0.5f;
		const float dx = state.position[0] - c.pos[0], dz = state.position[2] - c.pos[2];
		const float horizontal = c.radius + reach;
		if (dx * dx + dz * dz > horizontal * horizontal)
			continue;
		if (air && (state.position[1] < c.pos[1] - reach || state.position[1] > c.pos[1] + c.height + reach))
			continue;
		s_attackTests++;
		bool hit;
		if (!AttackGuarded(marioId, c.pos[0], middle, c.pos[2], &hit))
			return false;
		if (!hit)
			continue;
		s_attackContacts++;
		// The fork's bounce-back sets forwardVel to -48 (ground) / -16 (air) on every punch/kick contact, which pushes
		// Mario ~170 units away, out of reach for the next punch of a combo. Scale it by fPunchRecoil.
		if ((state.action == ACT_PUNCHING || state.action == ACT_MOVE_PUNCHING || state.action == ACT_JUMP_KICK) &&
			!(state.flags & MARIO_METAL_CAP))
		{
			if (!ForwardVelocityGuarded(marioId, (air ? -16.0f : -48.0f) * g_config.punchRecoil))
				return false;
		}
		// One damage per strike: sm64_mario_attack answers true on every tick of a punch's active frames, so only the
		// first tick of an unbroken run counts, and iAttackCooldown sets the minimum gap between two strikes.
		const bool sameStrike = c.lastContactTick && s_attackTick - c.lastContactTick == 1;
		c.lastContactTick = s_attackTick;
		if (sameStrike || (c.hits && s_attackTick - c.lastHitTick < g_config.attackCooldown))
		{
			s_attackCooled++;
			continue;
		}

		// Base: vanilla bare-hand damage (iDamageMode 1) or SA's fixed 20. Move multipliers follow SA's table (kick 30,
		// ground pound 70, metal cap +200 over a 20 punch), plus the slide kick, which SA leaves at punch damage.
		float base = g_config.attackDamage;
		if (g_config.damageMode == 1 && !UnarmedDamageGuarded(player, &base))
		{
			SM64_LogFault("unarmed damage (0x00644CE0)");
			_ERROR("attack: the engine's unarmed damage function faulted, using fAttackDamage until the next spawn");
			s_unarmedFault = true;
		}
		if (g_config.damageMode != 1 || s_unarmedFault)
			base = g_config.attackDamage;
		float mult = 1.0f;
		bool knock = false;
		if (state.action == ACT_JUMP_KICK || state.action == ACT_SLIDE_KICK || state.action == ACT_SLIDE_KICK_SLIDE ||
			(state.action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE))
		{
			mult = g_config.kickMult;
			knock = true;
		}
		else if (state.action == ACT_GROUND_POUND)
			mult = state.velocity[1] < 0.0f ? g_config.groundPoundMult : 0.0f;
		if (state.flags & MARIO_METAL_CAP)
		{
			mult *= g_config.metalCapMult;
			knock = true;
		}
		const float raw = base * mult * g_config.damageMult;
		if (raw <= 0.0f)
			continue;

		const bool armor = g_config.damageMode == 1;
		if (armor && s_maxArmorRating < 0.0f)
			LoadArmorSettings();
		float source[3];
		Units::Sm64ToFnv(state.position, source[0], source[1], source[2]);
		HitResult result;
		float damage, health, dr, dt;
		bool knocked;
		if (!HitActorGuarded(c.refID, raw, armor, knock, source, &result, &damage, &health, &dr, &dt, &knocked))
		{
			SM64_LogFault("npc attack (DoDamage / KnockExplosion)");
			_ERROR("attack: damaging ref %08X faulted, attacks off until the next spawn", c.refID);
			Console_Print("SM64FNV: Mario's attacks off after a fault (see log)");
			s_attackFault = true;
			return true;
		}
		c.lastHitTick = s_attackTick;
		const char* name = AttackName(state.action, air);
		if (result == kHitGone)
		{
			s_attackGone++;
			AttackLog("attack: %s on ref %08X skipped, actor gone or no longer alive", name, c.refID);
			continue;
		}
		c.hits++;
		s_attackHits++;
		s_attackDamageDealt += damage;
		if (knocked)
			s_attackKnocks++;
		if (result == kHitKilled)
			s_attackKills++;
		const bool logged = s_attackLogLines < kMaxLogLines;
		AttackLog("attack: %s (action 0x%08X%s) hits ref %08X (base %08X): base %.2f x %.2f = %.2f, DR %.0f DT %.1f -> "
			"%.2f damage -> health %.1f%s%s, hit %u", name, state.action, (state.flags & MARIO_METAL_CAP) ? ", metal cap" : "",
			c.refID, c.baseID, base, mult * g_config.damageMult, raw, dr, dt, damage, health,
			result == kHitKilled ? ", killed" : result == kHitInvulnerable ? ", no health lost (invulnerable/essential?)" : "",
			knocked ? ", knocked down" : knock ? ", no knockdown (not high process or iKnockForce 0)" : "", c.hits);
		if (logged)
			Console_Print("SM64FNV: %s ref %08X -%.1f (health %.0f)%s%s", name, c.refID, damage, health,
				knocked ? ", knocked down" : "",
				result == kHitKilled ? ", killed" : "");
	}
	return true;
}

void NpcColliders_Clear(bool callLibsm64)
{
	if (!s_colliders.empty())
		_MESSAGE("npc: %u colliders %s", (uint32_t)s_colliders.size(), callLibsm64 ? "deleted" : "forgotten (libsm64 fault)");
	if (callLibsm64)
	{
		if (!DeleteAll())
			SM64_LogFault("npc collider delete");
	}
	else
	{
		s_deleted += (uint32_t)s_colliders.size();
		s_colliders.clear();
	}
	s_gameFault = false;
	s_attackFault = false;
	s_unarmedFault = false;
}

void NpcColliders_Dump()
{
	if (!g_config.npcColliders)
	{
		Console_Print("SM64FNV: NPC colliders off (ini)");
		_MESSAGE("state dump npc: off (ini)");
		return;
	}
	Console_Print("SM64FNV: NPCs: %u colliders (max %u), %u high actors, %u in range, nearest %.0f, %u contacts%s",
		(uint32_t)s_colliders.size(), g_config.maxNpcColliders, s_seen, s_inRange, s_nearest, s_contacts,
		s_gameFault ? "; OFF after a fault" : "");
	_MESSAGE("state dump npc: %u colliders, %u high actors seen, %u in range (%u over the scan buffer), nearest %.1f "
		"(ref %08X), %u created, %u deleted, %u moves, %u contacts, fault %d", (uint32_t)s_colliders.size(), s_seen,
		s_inRange, s_overflow, s_nearest, s_nearestRef, s_created, s_deleted, s_moves, s_contacts, s_gameFault);
	for (const Collider& c : s_colliders)
		_MESSAGE("  collider %u: ref %08X base %08X type %u sm64=(%.1f, %.1f, %.1f) r %.1f h %.1f, %u contacts%s, %u hits",
			c.id, c.refID, c.baseID, c.baseType, c.pos[0], c.pos[1], c.pos[2], c.radius, c.height, c.contacts,
			c.touching ? ", touching" : "", c.hits);
	if (!g_config.npcAttacks)
	{
		Console_Print("SM64FNV: attacks off (ini)");
		_MESSAGE("state dump attack: off (ini)");
		return;
	}
	Console_Print("SM64FNV: attacks: %u hits (-%.0f), %u kills, %u knockdowns, %u cooled down%s", s_attackHits,
		s_attackDamageDealt, s_attackKills, s_attackKnocks, s_attackCooled, s_attackFault ? "; OFF after a fault" : "");
	_MESSAGE("state dump attack: %u reach tests, %u attack contacts, %u hits (%.1f damage), %u cooled down, %u kills, "
		"%u knockdowns, %u gone, fault %d", s_attackTests, s_attackContacts, s_attackHits, s_attackDamageDealt,
		s_attackCooled, s_attackKills, s_attackKnocks, s_attackGone, s_attackFault);
}
