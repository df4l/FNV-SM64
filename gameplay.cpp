#include "gameplay.h"
#include "sm64_loader.h"
#include "config.h"
#include "game_addrs.h"
#include "units.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/Utilities.h"
#include "decomp/include/sm64shared.h"

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace
{
	// SM64 health: 0x880 full (8 wedges), below 0x100 dead. SA: full above 10 % of the player's health, 0x200 at or
	// below, killed at 0. The fraction is [Gameplay] fLowHealth here: FNV hits often skip a 10 % band. One hurtCounter unit costs 0x40 of 0x880 in SM64 (decomp mario.c), mapped onto the player's
	// health the same way.
	constexpr uint16_t kHealthFull = 0x880;
	constexpr uint16_t kHealthLow = 0x200;
	constexpr uint16_t kHealthDeadBelow = 0x100;
	constexpr float kHurtUnit = 64.0f / 2176.0f;
	constexpr uint32_t kMaxLogLines = 40;
	// Actor lifeState (gameplay.md §2): 1 DYING, 2 DEAD.
	constexpr UInt32 kLifeDying = 1, kLifeDead = 2;

	enum Band { kBandNone = -1, kBandDead, kBandLow, kBandFull, kBandSubmerged };
	const char* BandName(int band)
	{
		static const char* const names[] = { "dead", "low", "full", "submerged (full)" };
		return band >= 0 && band <= kBandSubmerged ? names[band] : "none";
	}

	int s_band = kBandNone;
	uint8_t s_lastHurt = 0;
	uint32_t s_logLines = 0;
	uint32_t s_hurtEvents = 0, s_revives = 0, s_kills = 0;
	float s_damageDealt = 0.0f;
	float s_current = 0.0f;
	UInt32 s_base = 0;
	bool s_gameFault = false;	// a game call faulted: health sync off until the next spawn

	void LogLine(const char* fmt, ...)
	{
		if (s_logLines >= kMaxLogLines)
			return;
		if (++s_logLines == kMaxLogLines)
			_MESSAGE("health: log cap reached (%u lines), F10 still reports", kMaxLogLines);
		char buffer[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		_MESSAGE("%s", buffer);
	}

	// Current (vt[3]) and base (vt[0]) HEALTH, as Actor::GetHealthPercentage 0x00893590 reads them (gameplay.md §2).
	bool ReadHealthGuarded(PlayerCharacter* player, float* current, UInt32* base)
	{
		__try
		{
			*base = player->avOwner.Fn_00(GameAddr::kAVHealth);
			*current = player->avOwner.Fn_03(GameAddr::kAVHealth);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	// Player's DamageModActorValue(HEALTH, -damage, no attacker): the engine kills below 1 health.
	bool DamagePlayerGuarded(PlayerCharacter* player, float damage)
	{
		__try
		{
			const UInt32 fn = (*reinterpret_cast<UInt32**>(player))[GameAddr::kVtblActorDamageModActorValue / 4];
			ThisStdCall<void>(fn, player, GameAddr::kAVHealth, -damage, static_cast<Actor*>(nullptr));
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	// --- M10.2 water (gameplay.md §6) ---
	constexpr float kNoWaterBelow = -1.0e30f;	// RenderState::waterLevel is -FLT_MAX without water
	uint32_t s_waterLogLines = 0;
	int s_inWater = -1;				// -1 unknown
	float s_waterLevel = 0.0f;		// game units, last read
	int32_t s_waterSent = INT32_MIN;	// last level given to Mario (SM64 units)
	uint32_t s_waterEntries = 0;
	bool s_waterFault = false;		// reading faulted: water off until the next spawn

	void WaterLog(const char* fmt, ...)
	{
		if (s_waterLogLines >= kMaxLogLines)
			return;
		if (++s_waterLogLines == kMaxLogLines)
			_MESSAGE("water: log cap reached (%u lines), F10 still reports", kMaxLogLines);
		char buffer[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer), fmt, args);
		va_end(args);
		_MESSAGE("%s", buffer);
	}

	// Actor::inWater and renderState->waterLevel: what 0x0057B0A0 returns and the char controller copies.
	bool ReadWaterGuarded(PlayerCharacter* player, bool* inWater, float* level)
	{
		__try
		{
			*inWater = *(reinterpret_cast<const UInt8*>(player) + GameAddr::kActorInWater) != 0;
			const uint8_t* renderState = reinterpret_cast<const uint8_t*>(player->renderState);
			*level = renderState ? *reinterpret_cast<const float*>(renderState + GameAddr::kRenderStateWaterLevel) : -FLT_MAX;
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool SetWaterGuarded(int32_t id, int32_t level)
	{
		__try
		{
			g_sm64.set_mario_water_level(id, level);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	// SA: level = bTouchingWater ? m_fWaterHeight / scale : INT16_MIN. Returns false if libsm64 faulted.
	bool UpdateWater(int32_t marioId, PlayerCharacter* player, const SM64MarioState& state)
	{
		bool inWater = false;
		float level = -FLT_MAX;
		if (!s_waterFault && !ReadWaterGuarded(player, &inWater, &level))
		{
			SM64_LogFault("water read (Actor::inWater / renderState)");
			_ERROR("water: reading the player's water state faulted, water off until the next spawn");
			s_waterFault = true;
		}
		const bool hasLevel = level > kNoWaterBelow && level < FLT_MAX;
		const bool use = hasLevel && (inWater || !g_config.waterNeedsContact) && !s_waterFault;
		const int32_t sent = use ? (int32_t)lroundf(level / Units::kFnvPerSm64) : INT16_MIN;

		if ((int)inWater != s_inWater)
		{
			if (inWater)
				s_waterEntries++;
			if (s_inWater >= 0)
				WaterLog("water: player %s water (level %s%.1f, Mario fnv z %.1f, action 0x%08X)", inWater ? "entered" : "left",
					hasLevel ? "" : "none ", hasLevel ? level : 0.0f, state.position[1] * Units::kFnvPerSm64, state.action);
			s_inWater = inWater;
		}
		if (sent != s_waterSent)
		{
			if (use)
				WaterLog("water: Mario water level %d (fnv z %.1f, player in water %d)", sent, level, inWater);
			else
				WaterLog("water: Mario water level off (%s)", s_waterFault ? "fault" : !hasLevel ? "no water here" : "player not in water");
			if (use && (s_waterSent == INT32_MIN || s_waterSent == INT16_MIN))
				Console_Print("SM64FNV: water at z %.0f", level);
			s_waterSent = sent;
		}
		s_waterLevel = level;
		return SetWaterGuarded(marioId, sent);
	}

	// kill / revive (spawn spin) / set health, as SA does every tick.
	bool ApplyHealthGuarded(int32_t id, int band, bool revive)
	{
		__try
		{
			if (band == kBandDead)
				g_sm64.mario_kill(id);
			else
			{
				if (revive)
					g_sm64.set_mario_action(id, ACT_SPAWN_SPIN_AIRBORNE);
				g_sm64.set_mario_health(id, band == kBandLow ? kHealthLow : kHealthFull);
			}
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}
}

void Gameplay_Reset()
{
	s_band = kBandNone;
	s_lastHurt = 0;
	s_gameFault = false;
	s_inWater = -1;
	s_waterSent = INT32_MIN;
	s_waterFault = false;
}

// M10.1 health sync. Returns false if libsm64 faulted.
static bool UpdateHealth(int32_t marioId, PlayerCharacter* player, const SM64MarioState& state)
{
	if (s_gameFault)
		return true;
	float current;
	UInt32 base;
	if (!ReadHealthGuarded(player, &current, &base))
	{
		SM64_LogFault("health read (ActorValueOwner)");
		_ERROR("health: reading the player's health faulted, health sync off until the next spawn");
		s_gameFault = true;
		return true;
	}
	s_current = current;
	s_base = base;
	const float ratio = base ? current / (float)base : 1.0f;
	const bool playerDead = ratio <= 0.0f || player->lifeState == kLifeDying || player->lifeState == kLifeDead;

	int band;
	if (playerDead)
		band = kBandDead;
	else if ((state.action & ACT_GROUP_MASK) == ACT_GROUP_SUBMERGED)
		band = kBandSubmerged;
	else
		band = ratio <= g_config.lowHealth ? kBandLow : kBandFull;
	const bool revive = band != kBandDead && state.health < kHealthDeadBelow;

	if (band != s_band)
	{
		LogLine("health: player %.1f / %u (%.0f %%, life state %u) -> Mario %s (was %s, Mario health 0x%04X)", current, base,
			ratio * 100.0f, player->lifeState, BandName(band), BandName(s_band), (uint16_t)state.health);
		if (band == kBandDead)
		{
			s_kills++;
			Console_Print("SM64FNV: player dead, Mario killed");
		}
		s_band = band;
	}
	if (revive)
	{
		s_revives++;
		LogLine("health: Mario dead (0x%04X) while the player lives (%.1f / %u): revived with a spawn spin",
			(uint16_t)state.health, current, base);
	}
	if (!ApplyHealthGuarded(marioId, band, revive))
		return false;

	// Mario's own damage (falls; later attacks) -> the player, once per hurtCounter rise as SA does. Not while dead.
	if (state.hurtCounter && !s_lastHurt && band != kBandDead)
	{
		const float damage = kHurtUnit * state.hurtCounter * (float)base;
		s_hurtEvents++;
		s_damageDealt += damage;
		LogLine("health: Mario hurt (hurtCounter %u, action 0x%08X) -> player loses %.1f health (%.1f / %u before)",
			state.hurtCounter, state.action, damage, current, base);
		Console_Print("SM64FNV: Mario hurt, player -%.0f health", damage);
		if (!DamagePlayerGuarded(player, damage))
		{
			SM64_LogFault("player damage (DamageModActorValue)");
			_ERROR("health: damaging the player faulted, health sync off until the next spawn");
			s_gameFault = true;
		}
	}
	s_lastHurt = state.hurtCounter;
	return true;
}

bool Gameplay_AfterTick(int32_t marioId, const SM64MarioState& state)
{
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player)
		return true;
	if (g_config.healthSync && !UpdateHealth(marioId, player, state))
		return false;
	return !g_config.water || UpdateWater(marioId, player, state);
}

void Gameplay_DumpState()
{
	if (!g_config.healthSync)
	{
		Console_Print("SM64FNV: health sync off (ini)");
		_MESSAGE("state dump health: off (ini)");
	}
	else
	{
		const float ratio = s_base ? s_current / (float)s_base : 1.0f;
		Console_Print("SM64FNV: health %.0f / %u (%.0f %%) -> Mario %s; %u hurt events (-%.0f), %u kills, %u revives%s",
			s_current, s_base, ratio * 100.0f, BandName(s_band), s_hurtEvents, s_damageDealt, s_kills, s_revives,
			s_gameFault ? "; OFF after a fault" : "");
		_MESSAGE("state dump health: player %.2f / %u (%.1f %%), Mario band %s, %u hurt events (%.1f damage), %u kills, "
			"%u revives, fault %d", s_current, s_base, ratio * 100.0f, BandName(s_band), s_hurtEvents, s_damageDealt, s_kills,
			s_revives, s_gameFault);
	}

	if (!g_config.water)
	{
		Console_Print("SM64FNV: water off (ini)");
		_MESSAGE("state dump water: off (ini)");
		return;
	}
	const bool hasLevel = s_waterLevel > kNoWaterBelow && s_waterLevel < FLT_MAX;
	Console_Print("SM64FNV: water: player in water %d, level %s%.0f, Mario level %s, %u entries%s", s_inWater,
		hasLevel ? "z " : "none ", hasLevel ? s_waterLevel : 0.0f, s_waterSent == INT16_MIN ? "off" : "on", s_waterEntries,
		s_waterFault ? "; OFF after a fault" : "");
	_MESSAGE("state dump water: player inWater %d, level %s%.2f, Mario water level %d, %u entries, needs contact %d, fault %d",
		s_inWater, hasLevel ? "" : "none ", hasLevel ? s_waterLevel : 0.0f, s_waterSent, s_waterEntries,
		g_config.waterNeedsContact, s_waterFault);
}
