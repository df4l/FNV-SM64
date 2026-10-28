#pragma once

// One Mario instance. M2: spawned on a flat test floor at the player's feet, ticked with the player's input (M4);
// the player ref follows him (M2.2).

// Called once per 30 Hz tick while the simulation runs; spawns Mario first if needed.
void Mario_Tick();
// alpha in [0, 1): how far the frame is from the previous tick to the latest one ([Render] bInterpolate).
// Once per running frame after the ticks: moves the player ref onto Mario ([Subsystems] bPlayerFollow).
void Mario_SyncPlayer(float alpha);
// Once per running frame after Mario_SyncPlayer: hands Mario's mesh to the render (M3).
void Mario_UpdateRender(float alpha);
// Master toggle off, game unloaded, or shutdown. Safe to call when not spawned.
void Mario_Despawn(const char* reason);
// Re-arms spawning after a failure (called when the master toggle turns on).
void Mario_ResetFailure();
void Mario_DumpState();
bool Mario_IsSpawned();
