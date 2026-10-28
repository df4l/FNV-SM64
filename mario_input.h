#pragma once

struct SM64MarioInputs;

// Mario's inputs from the game's own control bindings (OSInputGlobals::GetControlState), so remapping in the game's
// settings applies to Mario too. Movement keys or the gamepad left stick (analog), Jump/Attack/Sneak, Run held = walk
// (keys only; the stick tilt sets the speed).
// The stick is relative to the world camera's facing. Called once per 30 Hz tick.
void MarioInput_Read(SM64MarioInputs& out);
// On spawn: restarts the change log.
void MarioInput_Reset();
void MarioInput_DumpState();
