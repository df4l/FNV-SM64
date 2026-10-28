#pragma once

// Fixed 30 Hz SM64 clock, driven from kMessage_MainGameLoop and gated on GameState::SimulationRunning.
void Tick_OnMainGameLoop();
void Tick_DumpState();

// Master toggle: while inactive SM64 never ticks and Mario is despawned (later: vanilla player restored too).
void Tick_InitActive(bool active);	// PostLoad, before the console exists: log only
void Tick_SetActive(bool active);
bool Tick_IsActive();
