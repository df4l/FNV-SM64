#include "player_control.h"
#include "game_addrs.h"
#include "config.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/SafeWrite.h"
#include "nvse/Utilities.h"

namespace
{
	// Movement (also blocks jump) and sneak: the player's input otherwise moves them after our snap, before the frame
	// is drawn (M2.2 jitter), and sneak still toggled in M2.3. Not Fight: the AI procedures 0x008ECF20 and 0x00918120
	// give up on the player as a target while it is disabled (M10.1, Ghidra 2026-10-04); the control filter blocks
	// Attack instead. Not POVSwitch (the setter forces 1st person) and not Look (the camera needs the mouse).
	constexpr UInt8 kOwnedMask = PlayerCharacter::kControlFlag_Movement | PlayerCharacter::kControlFlag_Sneak;

	UInt8 s_ownedBits = 0;	// bits we disabled and must give back
	UInt32 s_saveGameTarget = 0;	// call target found at the patched site (normally kBGSSaveLoadGameSaveGame)

	void SetDisabled(PlayerCharacter* player, bool disable, UInt8 mask)
	{
		// The game reads the mask as a full dword (OR EDX,[EBP+0xC]), so both args are passed as UInt32.
		ThisStdCall<void>(GameAddr::kPlayerSetControlsDisabled, player, (UInt32)disable, (UInt32)mask);
	}

	// Replaces the __thiscall SaveGame(arg) call. The flag byte is edited directly (not through the setter) so the HUD
	// doesn't flicker; the bits are back before the next frame.
	void __fastcall SaveGameHook(void* saveLoadGame, void*, UInt32 arg)
	{
		PlayerCharacter* player = PlayerCharacter::GetSingleton();
		const UInt8 hidden = player ? (player->disabledControlFlags & s_ownedBits) : 0;
		if (hidden)
			player->disabledControlFlags &= ~hidden;
		ThisStdCall<void>(s_saveGameTarget, saveLoadGame, arg);
		if (hidden)
		{
			player->disabledControlFlags |= hidden;
			_MESSAGE("save: wrote the save without our control bits 0x%02X", hidden);
		}
	}

	// Flag readers that would otherwise count our bits: Wait refused silently, HUD hidden (on menu close and inside
	// HUDMainMenu::UpdateVisibilityState).
	bool s_waitLogged = false;

	UInt8 __fastcall WaitFlagsHook(PlayerCharacter* player)
	{
		const UInt8 flags = player->disabledControlFlags;
		if ((flags & s_ownedBits) && !s_waitLogged)
		{
			s_waitLogged = true;
			_MESSAGE("wait: key pressed with our control bits 0x%02X set, ignored for the check (flags 0x%02X)", s_ownedBits, flags);
		}
		return flags & ~s_ownedBits;
	}

	bool __fastcall HudMovementCheckHook(PlayerCharacter* player, void*, UInt32 mask)
	{
		const UInt8 owned = g_config.showHud ? s_ownedBits : 0;
		return (player->disabledControlFlags & ~owned & (UInt8)mask) != 0;
	}

	// Activate is refused (fail sound) while Movement is disabled; Mario owns that bit, so it doesn't count there.
	bool s_activateLogged = false;

	bool __fastcall ActivateMovementCheckHook(PlayerCharacter* player, void*, UInt32 mask)
	{
		const UInt8 flags = player->disabledControlFlags;
		if ((flags & s_ownedBits & (UInt8)mask) && !s_activateLogged)
		{
			s_activateLogged = true;
			_MESSAGE("activate: pressed with our control bits 0x%02X set, ignored for the check (flags 0x%02X)", s_ownedBits, flags);
		}
		return (flags & ~s_ownedBits & (UInt8)mask) != 0;
	}

	// NPC combat skips a target whose Movement is disabled (cutscene rule); our bit must not make Mario untouchable.
	UInt32 s_combatChecks = 0;

	bool __fastcall CombatMovementCheckHook(PlayerCharacter* player, void*, UInt32 mask)
	{
		const UInt8 flags = player->disabledControlFlags;
		if (flags & s_ownedBits & (UInt8)mask)
		{
			if (!s_combatChecks)
				_MESSAGE("combat: an NPC targeting the player checked Movement; our bits 0x%02X ignored (flags 0x%02X)",
					s_ownedBits, flags);
			s_combatChecks++;
		}
		return (flags & ~s_ownedBits & (UInt8)mask) != 0;
	}

	struct CallPatch
	{
		UInt32 site;
		UInt32 expectedTarget;
		void* hook;
		const char* name;
		bool installed;
	};
	CallPatch s_flagPatches[] = {
		{ GameAddr::kWaitFlagsCallSite, GameAddr::kPlayerGetControlFlags, (void*)WaitFlagsHook, "wait check", false },
		{ GameAddr::kHudMovementCheckCallSite, GameAddr::kPlayerIsControlDisabled, (void*)HudMovementCheckHook, "hud check", false },
		{ GameAddr::kHudVisMovementCheckSite1, GameAddr::kPlayerIsControlDisabled, (void*)HudMovementCheckHook, "hud state check 1", false },
		{ GameAddr::kHudVisMovementCheckSite2, GameAddr::kPlayerIsControlDisabled, (void*)HudMovementCheckHook, "hud state check 2", false },
		{ GameAddr::kActivateMovementCheckSite, GameAddr::kPlayerIsControlDisabled, (void*)ActivateMovementCheckHook, "activate check", false },
		{ GameAddr::kCombatMovementCheckSite, GameAddr::kPlayerIsControlDisabled, (void*)CombatMovementCheckHook, "combat check", false },
	};
}

bool PlayerControl_InstallSaveHook()
{
	const UInt32 site = GameAddr::kSaveGameCallSite;
	if (*(UInt8*)site != 0xE8)
	{
		_ERROR("save hook: no call at %08X (byte %02X), not installed", site, *(UInt8*)site);
		return false;
	}
	s_saveGameTarget = site + 5 + *(SInt32*)(site + 1);
	// Another plugin may already have redirected this call; chaining to its target keeps both working.
	if (s_saveGameTarget != GameAddr::kBGSSaveLoadGameSaveGame)
		_WARNING("save hook: call at %08X already redirected to %08X, chaining", site, s_saveGameTarget);
	WriteRelCall(site, (UInt32)SaveGameHook);
	_MESSAGE("save hook installed at %08X (original target %08X)", site, s_saveGameTarget);
	return true;
}

void PlayerControl_InstallFlagPatches()
{
	for (CallPatch& patch : s_flagPatches)
	{
		const UInt32 site = patch.site;
		const UInt32 target = *(UInt8*)site == 0xE8 ? site + 5 + *(SInt32*)(site + 1) : 0;
		// Our replacement doesn't call the original, so a call another plugin already redirected is left alone.
		if (target != patch.expectedTarget)
		{
			_WARNING("%s patch: expected call %08X at %08X, found byte %02X target %08X; not installed", patch.name,
				patch.expectedTarget, site, *(UInt8*)site, target);
			continue;
		}
		WriteRelCall(site, (UInt32)patch.hook);
		patch.installed = true;
		_MESSAGE("%s patch installed at %08X (original target %08X)", patch.name, site, target);
	}
}

void PlayerControl_RemoveFlagPatches()
{
	for (CallPatch& patch : s_flagPatches)
	{
		if (!patch.installed)
			continue;
		WriteRelCall(patch.site, patch.expectedTarget);
		patch.installed = false;
		_MESSAGE("%s patch removed", patch.name);
	}
}

void PlayerControl_RemoveSaveHook()
{
	if (!s_saveGameTarget)
		return;
	WriteRelCall(GameAddr::kSaveGameCallSite, s_saveGameTarget);
	s_saveGameTarget = 0;
	_MESSAGE("save hook removed");
}

void PlayerControl_Acquire()
{
	if (s_ownedBits)
		return;
	if (!s_saveGameTarget)
	{
		// Our bits would end up in saves with nothing to strip them.
		_WARNING("player controls left enabled: save hook not installed");
		return;
	}
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player)
		return;
	const UInt8 before = player->disabledControlFlags;
	// Bits a script already disabled stay the script's: we neither set nor later clear them.
	s_ownedBits = kOwnedMask & ~before;
	if (s_ownedBits)
		SetDisabled(player, true, s_ownedBits);
	_MESSAGE("player controls: flags 0x%02X -> 0x%02X (we own 0x%02X)", before, player->disabledControlFlags, s_ownedBits);
	// The setter hid the HUD for our Movement bit; show it again as the setter does when nobody else holds Movement.
	if (g_config.showHud && (s_ownedBits & PlayerCharacter::kControlFlag_Movement) &&
		!(player->disabledControlFlags & ~s_ownedBits & PlayerCharacter::kControlFlag_Movement))
	{
		CdeclCall<void>(GameAddr::kHudUpdateVisibilityState, GameAddr::kHudVisibilityNormal);
		_MESSAGE("hud: visibility set back to normal");
	}
}

void PlayerControl_Release(const char* reason)
{
	if (!s_ownedBits)
		return;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (player)
	{
		const UInt8 before = player->disabledControlFlags;
		SetDisabled(player, false, s_ownedBits);
		_MESSAGE("player controls restored (%s): flags 0x%02X -> 0x%02X", reason, before, player->disabledControlFlags);
	}
	s_ownedBits = 0;
}

void PlayerControl_DumpState()
{
	const PlayerCharacter* player = PlayerCharacter::GetSingleton();
	const UInt8 flags = player ? player->disabledControlFlags : 0;
	Console_Print("SM64FNV: player control flags 0x%02X, we own 0x%02X; %u NPC combat checks passed", flags, s_ownedBits,
		s_combatChecks);
	_MESSAGE("state dump player control flags 0x%02X, we own 0x%02X, %u NPC combat checks passed", flags, s_ownedBits,
		s_combatChecks);
}
