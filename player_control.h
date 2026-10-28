#pragma once

// Disables the player's own movement and sneak while Mario drives them (M2.3), and gives them back.
// The game saves PlayerCharacter::disabledControlFlags (+0x680), and the NVSE co-save is unavailable without an
// assigned opcode base, so the game's one SaveGame call is wrapped to write the save without our bits.

// Patches the SaveGame call site; returns false (and logs) if the bytes there aren't the expected call.
bool PlayerControl_InstallSaveHook();
// Restores the original call site bytes.
void PlayerControl_RemoveSaveHook();
// Redirects the Wait key's flag check and the post-menu HUD check so our bits don't count (each call site checked).
void PlayerControl_InstallFlagPatches();
void PlayerControl_RemoveFlagPatches();
// Mario spawned with player follow on: disable the bits in kOwnedMask that aren't already disabled.
void PlayerControl_Acquire();
// Re-enables only the bits Acquire disabled. Safe to call when nothing is owned.
void PlayerControl_Release(const char* reason);
void PlayerControl_DumpState();
