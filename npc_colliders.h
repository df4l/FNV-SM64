#pragma once

#include <cstdint>
#include "libsm64.h"

// M10.3: living NPCs and creatures near Mario as libsm64 object colliders (fork's sm64_object_*), so they push him
// back as SA's peds do. Actors come from ProcessLists' high-process list (FNV-SM64-RE findings/gameplay.md §4).
// [Gameplay] bNpcColliders, iMaxNpcColliders, fNpcRange, fNpcRadius, fNpcHeight.
// M10.4: Mario's attacks on those actors (sm64_mario_attack, then DoDamage + KnockExplosion, gameplay.md §3).
// [Gameplay] bNpcAttacks, fAttackReach*, iAttackCooldown, damage bonuses, iKnockForce.

// Before each sm64_mario_tick: keeps one collider on each of the nearest living actors, moved to its feet.
// `marioPos` is Mario's last SM64 position. False if libsm64 faulted (the caller drops Mario).
bool NpcColliders_Update(const float marioPos[3]);
// After each sm64_mario_tick: SA's per-ped attack test on every collider actor in reach. False if libsm64 faulted.
bool NpcColliders_AfterTick(int32_t marioId, const SM64MarioState& state);
// Despawn: deletes the colliders in libsm64 (unless it faulted: `callLibsm64` false) and resets the fault latch.
void NpcColliders_Clear(bool callLibsm64);
void NpcColliders_Dump();
