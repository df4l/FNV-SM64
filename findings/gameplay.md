# P9: Gameplay (DONE, 2026-10-01)

PC = FalloutNV.exe 1.4.0.525 (Ghidra); Xbox = Fallout_Debug.exe (IDA, PDB). P8 (audio) was skipped at the user's request.

## 0. What the SA mod needs here (from `sm64-san-andreas/mario.cpp`)
| SA feature | SA code | FNV equivalent (this note) |
|---|---|---|
| Health ratio → Mario HP (0x880 full / 0x200 low / kill at 0) | `m_fHealth / m_fMaxHealth` | `Actor::GetHealthPercentage` §2 |
| Mario `hurtCounter` → damage the player | `ped->m_fHealth -= dmg` | `DamageModActorValue(HEALTH, -dmg, attacker)` §2 |
| Mario attack hits a ped (`sm64_mario_attack`) → damage + knockdown | `CWeapon::GenerateDamageEvent` | `Actor::DoDamage` + `KnockExplosion` §3 |
| NPCs as Mario colliders (`sm64_object_create/move`, fork only) | `CWorld::FindObjectsIntersectingCube`, `IsAlive()` | `ProcessLists` actor lists + IsDead §4 |
| Wall punch → break object | `CObject::ObjectDamage` | `TESObjectREFR::DamageObject` §5 |
| Water level | `bTouchingWater ? m_fWaterHeight : INT16_MIN` | `Actor+0x14C` + `bhkCharacterController+0x53C` §6 |
| Fire / burning | `m_pFire` | no FNV equivalent; skipped (§8) |

## 1. Vtable alignment Xbox ↔ PC (Actor, "TESForm" primary vtable)
PC has **two extra Actor virtuals** compared with the Xbox build, so in this region PC slot = Xbox slot + 4, then + 8:
- Form/REFR part: Xbox ≤0x100 matches PC (`IsActor` 0x100 on both); **+4 from ≤0x140** (Xbox `DamageObject` 0x140 → PC 0x144, `IsChild?`/0x19C → 0x1A0, `IsDead` 0x228 → 0x22C).
- Actor part: **+4 for Xbox 0x334..0x3F4** (one extra PC slot somewhere between 0x100 and 0x140; exact slot not identified), **+8 from Xbox ≥0x410** (second extra PC slot between Xbox 0x3F8 and 0x410). JIP 0x354 `GetDefaultTurningSpeed` = Xbox 0x350 (`_purecall` on Actor) +4, so it is *not* an extra slot.
- Verified pairs (PC body checked or PC override pattern matches Xbox): DoDamage 0x334→0x338, IsOverEncumbered 0x354→0x358, IsPc 0x35C→0x360, GetRace 0x378→0x37C, GetActorType (pure on Actor) 0x38C→0x390, TempMod(float) 0x398→0x39C, PermMod(int) 0x3A4→0x3A8, DamageMod(float) 0x3A8→0x3AC, DamageMod(int) 0x3AC→0x3B0, GetCombatGroup 0x3F4→0x3F8, InitiateGetUpPackage 0x410→0x418, HasBeenAttacked 0x468→0x470, HitMe 0x470→**0x478** (not verified on PC), IsImmobile 0x4AC→0x4B4, HandleHealthDamage 0x4B0→0x4B8.
- PlayerCharacter overrides exactly the same six AV-mod slots (PC 0x39C–0x3B0 / Xbox 0x398–0x3AC) on both builds.
- **Process vtable (Base/MiddleHigh/HighProcess) has no shift** in the knock block (0x40C–0x420).

## 2. Health: read / write / death
`ActorValue::Index`: **HEALTH = 0x10 (16)**, FATIGUE = 0x16 (22), ACTION_POINTS = 0x0C (Xbox enum; PC code uses the same constants).

| Function | PC | Xbox | Signature / notes | Conf. |
|---|---|---|---|---|
| `ActorValueOwner` sub-object | `Actor+0xA4` | `Actor::ActorValueOwner` | AVO vtable: [0] GetBaseActorValue(int), [1] GetBaseActorFloatValue, [2] GetActorValue(int), [3] **GetActorFloatValue** (current), [4] GetTemporaryModifier, [5] GetDamageModifier, [6] GetPermanentModifier, [7] GetPermanentActorValue(int), [8] **GetPermanentActorFloatValue** (max), [9] GetAsForm, [10] GetActorLevel | high (Xbox vtable + JIP order + PC calls `[[this+0xA4]+0xC](0x10)` / `[[this+0xA4]](0x10)`) |
| `Actor::GetHealthPercentage` | **0x00893590** | 0x82BF3898 | `float __thiscall()`: current HEALTH (float) / base HEALTH (int); 1.0 if base = 0 | high (1:1 body; GetHealthPercentage condition 0x59C7E0) |
| `Actor::DamageModActorValue` (float) | **0x00881130**, vt+0x3AC | 0x82BDC3E8, vt+0x3A8 | `void __thiscall(int av, float delta, Actor* attacker)`; delta < 0 = damage. HEALTH && delta<0 → vt+0x4B8 HandleHealthDamage. Dirty flag 0x200000. JIP calls it `DamageActorValue` | high |
| `Actor::DamageModActorValue_int` | 0x00880FB0, vt+0x3B0 | 0x82BDC1C8, vt+0x3AC | int overload | high |
| `PlayerCharacter::DamageModActorValue` / `_int` | 0x0093B7A0 / 0x0093B3A0 | ?DamageModActorValue@PlayerCharacter | overrides | high |
| `Actor::TempModActorValue` (float) | 0x00880AD0, vt+0x39C | vt+0x398 | flag 0x100000 | medium-high |
| `Actor::PermanentModActorValue_int` | 0x00880C70, vt+0x3A8 | vt+0x3A4 | flag 0x800000 | medium-high |
| `Actor::HandleHealthDamage` | **0x00898650**, vt+0x4B8 | 0x82BF9B88, vt+0x4B0 | `void __thiscall(Actor* attacker, float dmg)`: if !IsDead: follower barks (<0.5, <0.25), ally-attacker filter, **Kill if health < 1** (or queue). JIP: `DoHealthDamage` | high (1:1 body) |
| `PlayerCharacter::HandleHealthDamage` | **0x009508F0** | — | calls Actor version, then IsDead. **Best "player was hurt" hook** (fires for every health loss through the AV system, with attacker + amount) | high |
| `Actor::Kill` | **0x0089D900** | 0x82BFFC58 `?Kill@Actor@@QAAXPAV1@M@Z` | `void __thiscall(Actor* killer, float dmg)`. Early-out if IsDead (vt+0x22C) or (vt+0x1A0 pred && not player). Writes killer @+0xC0, lifeState @+0x108 | high (strings "MUSDeath", "%.20s is dead!", "was killed by", essential icon) |
| `AILinearTaskThreadManager::ShouldQueue3DTask` | 0x008C7AA0 | 0x82C39558 | `bool()`; when true, Kill is deferred via QueueActorKill | high |
| `TaskQueueInterface::QueueActorKill` | 0x0087AFF0 | 0x82BD2F58 | `(Actor*, float* dmg, DISMEMBER_DATA*)`, task type 0xE | high |
| `Actor::IsDead` | vt+0x22C (arg 0) | vt+0x228 | see also P2 `Actor::IsDead` 0x8844F0 / lifeState +0x108 | high |
| `ACTOR_LIFE_STATE` | Actor+0x108 | Actor+0x118 | 0 ALIVE, 1 DYING, 2 DEAD, 3 UNCONSCIOUS, 4 REANIMATE, 5 RESTRAINED, **6 ESSENTIAL_DOWN** (PC Kill / KillActor test ==6) | high |
| Script cmd `DamageActorValue` | 0x005BDF20 (`Cmd_DamageActorValue`) | — | HEALTH && vt+0x1A0(0) → vt+0x4B8(0,-dmg) directly; else vt+0x3AC(av, -\|dmg\|, 0) | high |
| Script cmd `KillActor` | 0x005BE2A0 (`Cmd_KillActor`) | — | → Actor::Kill, or 0x8B51B0 for a dismember-kill | high |

**Mapping for the plugin (documentation only):** Mario HP from `GetHealthPercentage()` as SA does; Mario `hurtCounter` → `DamageModActorValue(0x10, -dmg, attacker=null)` on the player (death handled by the engine through HandleHealthDamage → Kill). Game-side hits on the player → hook `PlayerCharacter::HandleHealthDamage` (0x9508F0) to call `sm64_mario_take_damage`. One edge case: the "invulnerable/god" style predicate vt+0x1A0 (§8).

## 3. Mario hits an NPC (damage + knockback)
| Function | PC | Xbox | Signature / notes | Conf. |
|---|---|---|---|---|
| `Actor::DoDamage` | **0x0089D6F0**, vt+0x338 | 0x82BFF970, vt+0x334 | `bool __thiscall(float health, float fatigue, Actor* attacker)`: MagicTarget invulnerability check (`[this+0x94]` vt+0x10), aggro hooks on attacker/target combat controllers (vt+0x428), `DamageModActorValue(HEALTH, -h, attacker)` + `DamageObject(h, 0)`, fatigue via AV 0x16; returns IsDead. JIP: `DamageHealthAndFatigue`. **Cleanest "punch NPC" call**: `npc->DoDamage(dmg, 0, g_thePlayer)` → the NPC gets hostile, can die, gets credit/karma via Kill(killer) | high |
| `ActorHitData` | — | — | JIP layout (0x64): source, target, projectile, weaponAV, hitLocation, healthDmg… Only needed for full combat-style hits (hit events, crits); not needed for DoDamage | JIP only |
| `MiddleHighProcess::KnockExplosion` | **0x0091FEE0** (function created in P9), process vt+0x418 | 0x82CAF678, vt+0x418 | `void __thiscall(Actor* target, NiPoint3 sourcePos /*by value, 3 floats*/, float force)`. If `ShouldQueue3DTask` it queues; else if `Actor::CanKnockDown` (user name): when vt+0x38C(target) is false → char-controller push away from sourcePos; else ragdoll impulse (writes byte 2 = EXPLODE_LEAD_IN at +0x13C of an object that looks like the process; that object is not verified) | high (vtable index identical; caller Cmd_PushActorAway) |
| `Cmd_PushActorAway` (user name) | 0x005D6B60 | — | force = `AdjustPushForce(&actor->avOwner, force)` (user name), process = `actor+0x68`, needs high process (process level 0), source pos = pusher vt+0x1F4 (`GetLocationOnReference`-like, returns NiPoint3*) → `process->KnockExplosion(target, pos, force)` | high |
| `MiddleHighProcess::GetKnockState` / `SetKnockState` / `ClearKnockState` | 0x008D8540 / 0x008D8560 / 0x00920B80, vt+0x40C/+0x410/+0x420 | same slots | `KNOCK_STATE_ENUM`: 0 NORMAL, 1 EXPLODE, 2 EXPLODE_LEAD_IN, 3 OUT, 4 OUT_LEAD_IN, 5 QUEUED, 6 GET_UP | high |
| `MiddleHighProcess::UpdateKnockState` | 0x00920150, vt+0x41C | same | pre-existing Ghidra name | — |
| JIP `Actor::PushActor` (light push without ragdoll) | writes `charCtrl+0x500` VelocityMod, `+0x524` fVelocityTime, `|= 0x80` @ +0x417 | Xbox `bhkCharacterController::VelocityMod` +0x500, `fVelocityTime` +0x524 | Alternative "shove" without knockdown; offsets confirmed by Xbox type | high (offsets) |

SA ↔ FNV: SA's "knock ped to the ground" (jump kick, slide) ≈ `KnockExplosion(target, marioPos, force)`; ground pound/metal cap ≈ larger `DoDamage`.

## 4. Enumerating NPCs (colliders for `sm64_object_*`, attack targets)
| Item | PC | Xbox | Notes | Conf. |
|---|---|---|---|---|
| `g_ReferenceProcessLists` (ProcessLists) | **0x011E0E80** (label added) | `?ReferenceProcessLists@@3VProcessLists@@A` 0x8409A980 | JIP `ProcessManager::Get()`. PC Actor::Kill passes ECX=0x11E0E80 to 0x96E2F0 at the place Xbox calls `ProcessLists::RemoveAliveActor` | high |
| `+0x80 BSSimpleList<Actor*> AliveActorList` | — | same offset | high-process actors (JIP `highActors`). Iterate, skip player / IsDead / no 3D | high (Xbox type == JIP) |
| `+0x88 Actor* pActorsCloseToPlayer[50]`, `+0x150 int count` | — | same | already distance-filtered nearest actors: ideal for Mario colliders | high |
| `+0x10404 int iNumberHighActors` | — | Xbox | PC offset not verified | low |
| Actor position / radius | REFR pos +0x30 (P2); `CachedValuesOwner` (Xbox: CalculateCachedRadius, Width, Length, ForwardLength, …) | — | JIP's names for this vtable (GetRadius, GetWidthX, GetWidthY, **GetHeight**) disagree with Xbox at slot 3 (`ForwardLength`). Use a constant height like SA (`PED_HEIGHT`), or charController `fActorHeight`(+0x564 Xbox, unverified PC) | low (height) |

## 5. Breaking objects (wall attack, fork's `sm64_register_wall_attack_function`)
| Function | PC | Xbox | Notes | Conf. |
|---|---|---|---|---|
| `TESObjectREFR::DamageObject` | **0x00579220**, REFR vt+0x144 | 0x8270D280, vt+0x140 | `void __thiscall(float dmg, bool allowIfDestroyed)` | high (1:1 body; DamageObject command) |
| `BGSDestructibleObjectForm::DamageObject` | 0x00475B20 | 0x82579038 | `(TESObjectREFR*, float, bool)` | medium-high |
| `TESForm::GetDestroyed` | 0x00477BA0 | 0x8257CC88 | used by GetDestroyed condition | medium-high |
| `TESForm::GetDestructible` | 0x00452370 | 0x8252B4E8 | position in DamageObject | medium |
| Script cmd `DamageObject` | 0x005D4B90 (`Cmd_DamageObject`) | — | → `ref->DamageObject(dmg, true)` (direct) or queued | high |

The SA "health -= 500 then ObjectDamage" trick is not needed: FNV destructibles track damage per stage inside the destructible form.

## 6. Water
| Item | PC | Xbox | Notes | Conf. |
|---|---|---|---|---|
| `Actor::bInWater` | Actor+0x14C | +0x15C | — | high |
| `Actor::bSwimming` / `Actor::IsSwimming` | +0x14D / **0x005A2030** | +0x15D / 0x8274ACD0 | IsSwimming condition 0x5A1FA0 | high |
| `bhkCharacterController::fWaterHeight` | charCtrl+0x53C | +0x53C | surface Z in **Havok units**: written only by 0x0057B1D0 = `NI2HK_float(RenderState::waterLevel)` (Ghidra 2026-10-04) | high |
| `RenderState::waterLevel` (ref `renderState` +0x64 → +0x08) | getter 0x0057B0A0 (fastcall ref) | — | game units, −FLT_MAX without water. Set from the current water ref, else the cell height 0x005471E0 (cell flag 0x2, +0x50, FLT_MAX → worldspace default), else −FLT_MAX. Used by the plugin (M10.2) | high |
| `TES::GetWaterHeight(const NiPoint3&, TESObjectCELL*)` / `TESObjectCELL::GetWaterHeight` | not located on PC | 0x8253C7F0 / 0x826C36F8 | Only needed if Mario can be far from the player ref; JIP reads cell water height via `0x547770` (GetWaterForm) / cell fields | TODO |
Mapping: `sm64_set_mario_water_level(inWater ? fWaterHeight→SM64 : INT16_MIN)` exactly as SA.

## 7. Other state already known (P2) and reused here
IsDead 0x8844F0 / lifeState +0x108; GetSitSleepState 0x8D8800; SetControlsDisabled; 1st/3rd person. Knock state (§3) covers "player knocked down", which is the FNV analogue of SA's `overrideWithCJAI`.

## 8. Not done / caveats
- **Fire**: FNV has no burning-ped state like SA's `m_pFire`. Fire damage is a magic effect (ActiveEffect). Skipped; a plugin could trigger Mario's burn action from an effect check. Not reversed.
- **PC vt+0x1A0 predicate** (`Actor`, arg 0): used by Kill (`&& this != player` → no kill) and DamageActorValue (HEALTH → direct HandleHealthDamage). The Xbox symbol at the matching slot 0x19C is `?IsChild@Actor`, which doesn't fit; probably a folded/aliased PDB name. Meaning unknown (god mode / no-death?). **Low**.
- **PC vt+0x38C**: JIP says `IsPushable`; the Xbox slot (0x388) is `HasRagDoll`, and its use in KnockExplosion (ragdoll vs. char-controller branch) agrees with HasRagDoll. Medium; not renamed (vtable slot only).
- **0x008D8520** = `return *(this+0x68)` (= `MobileObject::GetCurrentAIProcess`), **1921 refs**, most likely COMDAT-folded; not renamed. The decompiler types its result as `Sky*` because of the user's `GetSun` name.
- **User name `GetSun` (0x0045CD60, 434 refs)** is used on process objects where Xbox calls `BaseProcess::GetProcessLevel` (0 = high). It is very likely a folded `return [this+0x28]` getter. **Not renamed** (user name); flagged in open-questions.
- Hit-reaction virtual `HitMe` (Xbox vt+0x470 → PC 0x478) not verified on PC.
