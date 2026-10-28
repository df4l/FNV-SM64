#include "control_filter.h"
#include "game_addrs.h"
#include "game_state.h"
#include "nvse/GameAPI.h"
#include "nvse/GameOSDepend.h"
#include "nvse/Utilities.h"
#include "nvse/SafeWrite.h"

#include <cstring>

namespace
{
	// ControlCode (Ghidra enum from the Stewie Tweaks PDB, matches findings/input.md §3): Attack, Aim/Block, ReadyItem
	// (ready weapon / reload), VATS, Hotkey1..8 (0x12 is AmmoSwap), Grab. Attack is blocked here rather than with the
	// Fight control flag, which makes the AI ignore the player (M10.1).
	bool IsBlockedCode(UInt32 code)
	{
		return code == 4 || code == 6 || code == 7 || code == 0x10 || (code >= 0x11 && code <= 0x18) || code == 0x1B;
	}
	constexpr UInt32 kCodeCount = 0x1C;

	bool s_installed = false;
	bool s_active = false;
	UInt8 s_originalBytes[GameAddr::kGetControlStatePrologueLen];
	UInt32 s_blocked[kCodeCount] = {};
	bool s_loggedCode[kCodeCount] = {};

	// The relocated prologue (push ebp; mov ebp, esp; sub esp, 0x14), then the rest of the original function.
	UInt32 s_resumeAddr = GameAddr::kGetControlState + GameAddr::kGetControlStatePrologueLen;
	__declspec(naked) int OriginalGetControlState()
	{
		__asm
		{
			push	ebp
			mov		ebp, esp
			sub		esp, 0x14
			jmp		dword ptr [s_resumeAddr]
		}
	}

	// __thiscall OSInputGlobals::GetControlState(code, state), ret 8, entered through the patched jump.
	int __fastcall GetControlStateHook(OSInputGlobals* self, void*, UInt32 code, UInt32 state)
	{
		if (s_active && IsBlockedCode(code) && GameState_Read().SimulationRunning())
		{
			const int result = ThisStdCall<int>((UInt32)OriginalGetControlState, self, code, state);
			if (result <= 0)
				return result;
			s_blocked[code]++;
			if (!s_loggedCode[code])
			{
				s_loggedCode[code] = true;
				_MESSAGE("control filter: first blocked press of control 0x%02X", code);
			}
			return 0;
		}
		return ThisStdCall<int>((UInt32)OriginalGetControlState, self, code, state);
	}
}

bool ControlFilter_Install()
{
	static const UInt8 kPrologue[GameAddr::kGetControlStatePrologueLen] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14 };
	const UInt32 entry = GameAddr::kGetControlState;
	if (memcmp((const void*)entry, kPrologue, sizeof(kPrologue)) != 0)
	{
		const UInt8* b = (const UInt8*)entry;
		_ERROR("control filter: unexpected bytes at %08X (%02X %02X %02X %02X %02X %02X), not installed (another plugin "
			"hooks it?)", entry, b[0], b[1], b[2], b[3], b[4], b[5]);
		return false;
	}
	memcpy(s_originalBytes, (const void*)entry, sizeof(s_originalBytes));
	WriteRelJump(entry, (UInt32)GetControlStateHook);
	SafeWrite8(entry + 5, 0x90);
	s_installed = true;
	_MESSAGE("control filter installed at %08X", entry);
	return true;
}

void ControlFilter_Remove()
{
	if (!s_installed)
		return;
	s_active = false;
	SafeWriteBuf(GameAddr::kGetControlState, s_originalBytes, sizeof(s_originalBytes));
	s_installed = false;
	_MESSAGE("control filter removed");
}

int ControlFilter_ReadUnfiltered(OSInputGlobals* input, UInt32 code, UInt32 state)
{
	return ThisStdCall<int>(s_installed ? (UInt32)OriginalGetControlState : GameAddr::kGetControlState, input, code, state);
}

void ControlFilter_SetActive(bool active, const char* reason)
{
	if (active == s_active)
		return;
	if (active && !s_installed)
	{
		_WARNING("control filter: not installed, weapon/VATS/hotkey controls stay live");
		return;
	}
	s_active = active;
	_MESSAGE("control filter %s (%s)", active ? "on" : "off", reason);
}

void ControlFilter_DumpState()
{
	UInt32 total = 0;
	for (UInt32 n : s_blocked)
		total += n;
	Console_Print("SM64FNV: control filter %s%s, %u blocked reads (attack %u, aim %u, ready %u, vats %u, grab %u)",
		s_installed ? "installed" : "NOT installed", s_active ? " and on" : ", off", total, s_blocked[4], s_blocked[6],
		s_blocked[7], s_blocked[0x10], s_blocked[0x1B]);
	_MESSAGE("state dump control filter: installed %d active %d, blocked total %u (attack %u, aim %u, ready %u, vats %u, "
		"grab %u)", s_installed, s_active, total, s_blocked[4], s_blocked[6], s_blocked[7], s_blocked[0x10], s_blocked[0x1B]);
}
