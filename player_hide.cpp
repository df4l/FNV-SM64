#include "player_hide.h"
#include "game_addrs.h"
#include "config.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/SafeWrite.h"

namespace
{
	using namespace GameAddr;

	uint8_t* s_root = nullptr;		// 3rd-person root we culled; only touched again while it is still the player's
	uint32_t s_reCulls = 0;			// frames where the game had shown the body again
	uint32_t s_reflectionSkips = 0;	// un-culls of s_root refused in the water reflection pass
	bool s_failed = false;
	bool s_reflectionPatched = false;

	void __fastcall ReflectionUncullHook(uint8_t* node, void*, UInt32 culled)
	{
		if (!culled && node && node == s_root)
		{
			if (++s_reflectionSkips == 1)
				_MESSAGE("player hide: water reflection tried to show the body, kept hidden");
			return;
		}
		reinterpret_cast<void(__thiscall*)(uint8_t*, UInt32)>(kNiAVObjectSetAppCulled)(node, culled);
	}

	uint8_t* ThirdPersonRoot(PlayerCharacter* player)
	{
		return player->renderState ? reinterpret_cast<uint8_t*>(player->renderState->niNode) : nullptr;
	}

	UInt32& Flags(uint8_t* node)
	{
		return *reinterpret_cast<UInt32*>(node + kAVFlags);
	}

	// Raw reads of game memory: a fault is logged and hiding stops instead of crashing the game.
	bool ApplyGuarded(PlayerCharacter* player, uint8_t** root, bool* culledNow)
	{
		__try
		{
			*root = ThirdPersonRoot(player);
			*culledNow = false;
			if (*root && !(Flags(*root) & kAVFlagAppCulled))
			{
				Flags(*root) |= kAVFlagAppCulled;
				*culledNow = true;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool ReleaseGuarded(PlayerCharacter* player, uint8_t* root, bool* stillOurs, bool* culled)
	{
		__try
		{
			*stillOurs = ThirdPersonRoot(player) == root;
			if (!*stillOurs)
				return true;
			uint8_t* firstPerson = reinterpret_cast<uint8_t*>(player->playerNode);
			*culled = firstPerson && !(Flags(firstPerson) & kAVFlagAppCulled);
			if (*culled)
				Flags(root) |= kAVFlagAppCulled;
			else
				Flags(root) &= ~kAVFlagAppCulled;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

void PlayerHide_Apply()
{
	if (!g_config.hidePlayer || s_failed)
		return;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player)
		return;
	uint8_t* root = nullptr;
	bool culledNow = false;
	if (!ApplyGuarded(player, &root, &culledNow))
	{
		_ERROR("player hide: faulted reading the player's 3D, hiding off for this session");
		Console_Print("SM64FNV: player hide crashed (see log)");
		s_root = nullptr;
		s_failed = true;
		return;
	}
	if (root != s_root)
	{
		_MESSAGE("player hide: 3rd-person root %p (was %p)%s", root, s_root, culledNow ? ", body hidden" : ", already culled");
		s_root = root;
		s_reCulls = 0;
	}
	else if (culledNow && ++s_reCulls <= 5)
		_MESSAGE("player hide: the game showed the body again (1st/3rd switch?), re-hidden (%u)", s_reCulls);
}

void PlayerHide_Release(const char* reason)
{
	if (!s_root)
		return;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	bool stillOurs = false, culled = false;
	if (!player || !ReleaseGuarded(player, s_root, &stillOurs, &culled))
		_ERROR("player hide: release (%s) %s", reason, player ? "faulted" : "found no player");
	else if (!stillOurs)
		_MESSAGE("player hide: release (%s): the player's 3D was rebuilt, nothing to restore", reason);
	else
		_MESSAGE("player hide: released (%s), 3rd-person root %s (1st-person root %s)", reason,
			culled ? "left culled" : "shown", culled ? "visible" : "culled or absent");
	s_root = nullptr;
}

void PlayerHide_DumpState()
{
	if (!g_config.hidePlayer)
	{
		Console_Print("SM64FNV: player hide off (ini)");
		return;
	}
	Console_Print("SM64FNV: player hide %s, root %p, re-hidden %u times, reflection un-culls blocked %u (patch %s)",
		s_failed ? "FAILED" : s_root ? "on" : "idle", s_root, s_reCulls, s_reflectionSkips, s_reflectionPatched ? "on" : "off");
	_MESSAGE("state dump player hide: %s, root %p, re-hidden %u, reflection blocked %u (patch %s)",
		s_failed ? "failed" : s_root ? "on" : "idle", s_root, s_reCulls, s_reflectionSkips, s_reflectionPatched ? "on" : "off");
}

void PlayerHide_InstallPatches()
{
	const UInt32 site = kReflectionUncullCallSite;
	const UInt32 target = *(UInt8*)site == 0xE8 ? site + 5 + *(SInt32*)(site + 1) : 0;
	// The hook calls SetAppCulled itself, so a call another plugin already redirected is left alone.
	if (target != kNiAVObjectSetAppCulled)
	{
		_WARNING("reflection cull patch: expected call %08X at %08X, found byte %02X target %08X; not installed",
			(UInt32)kNiAVObjectSetAppCulled, site, *(UInt8*)site, target);
		return;
	}
	WriteRelCall(site, (UInt32)ReflectionUncullHook);
	s_reflectionPatched = true;
	_MESSAGE("reflection cull patch installed at %08X (original target %08X)", site, target);
}

void PlayerHide_RemovePatches()
{
	if (!s_reflectionPatched)
		return;
	WriteRelCall(kReflectionUncullCallSite, kNiAVObjectSetAppCulled);
	s_reflectionPatched = false;
	_MESSAGE("reflection cull patch removed");
}
