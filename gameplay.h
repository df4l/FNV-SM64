#pragma once

#include <cstdint>
#include "libsm64.h"

// Gameplay rules run after each sm64_mario_tick on the game thread (M10, SA mario.cpp). FNV-SM64-RE findings/gameplay.md.

// At every spawn.
void Gameplay_Reset();
// M10.1 health sync and M10.2 water level. Returns false if libsm64 faulted (the caller drops Mario).
bool Gameplay_AfterTick(int32_t marioId, const SM64MarioState& state);
void Gameplay_DumpState();
