#pragma once

// Keeps the hidden player from reacting to attack/weapon/VATS/hotkey/grab controls while Mario is active (M4.3, M10.1).
// OSInputGlobals::GetControlState is hooked at its entry, so keyboard, mouse and gamepad are all covered; blocked
// codes read as "not pressed" only while the simulation runs (menus such as the Pip-Boy keep working).

// Patches the function entry; returns false (and logs) if its bytes aren't the expected prologue.
bool ControlFilter_Install();
// Restores the original prologue.
void ControlFilter_Remove();
// GetControlState without the filter (Mario's own input reads Attack). Works whether or not the hook is installed.
class OSInputGlobals;
int ControlFilter_ReadUnfiltered(OSInputGlobals* input, UInt32 code, UInt32 state);
// Mario spawned / despawned. Logs only on a change.
void ControlFilter_SetActive(bool active, const char* reason);
void ControlFilter_DumpState();
