# P2: Player (DONE, 2026-10-01)

PC = FalloutNV.exe 1.4.0.525 (Ghidra). Xbox = Fallout_Debug.exe (IDA). "(user)" = name set by the user before this mission.

## 1. Moving / teleporting the player

Two mechanisms exist. The SM64 per-frame "follow Mario" case needs the **light** one.

### A. Light: set reference position + physics capsule (per frame)

**Recommended single call: `MobileObject::SetPosition(const NiPoint3*)` @ 0x00931620 (user name; virtual).** It matches Xbox ?SetPosition@MobileObject@@ 0x82CC5D88 call-for-call:
SetLocationOnReference -> GetCharController -> bhkCharacterController::SetPosition(NiPoint3) -> bhkCharacterProxy::SetLinearVelocity(hkVector4::getZero()) -> QFallStartHeight -> QHitDamage -> ClearManifold -> `renderState->rootNode->SetTranslate(pos)`. Conf. **high**.

| Item | PC | Xbox | Signature | Conf. | How verified |
|---|---|---|---|---|---|
| `TESObjectREFR::SetLocationOnReference` (user name `SetLocationOnReference`) | 0x00575830 | ?SetLocationOnReference@TESObjectREFR@@ 0x82708228 | `void __thiscall(TESObjectREFR*, const NiPoint3*)` | high | user name + JIP `TESObjectREFR::SetPos` calls it first |
| `bhkCharacterController::SetPosition(const NiPoint3&)` | **0x005620E0** | 0x826EC628 | `void __thiscall(bhkCharacterController*, const NiPoint3*)` (game units) | high | identical body: hkVector4 ctor + NI2HK + SetPosition(hkVector4) |
| `bhkCharacterController::SetPosition(const hkVector4&)` | 0x00C6E390 | 0x832B5778 | adds RotCenter (+0x570) rotated by phantom transform, then inlined bhkCharacterProxy::SetPosition | high | body match |
| `hkpCharacterProxy::setPosition` | 0x00CAC920 | 0x83344608 | tolerance = m_keepContactTolerance(+0x5C) + m_keepDistance(+0x58) | high | body match |
| `hkpShapePhantom::setPosition(const hkVector4&, float)` | 0x00C9E990 | 0x83328910 | pos @ +0xE0, then updateBroadPhase | high | body match (+224) |
| `hkpPhantom::updateBroadPhase` | 0x00D07EC0 | 0x83439528 | | high | callee position |
| `NI2HK(hkVector4&, const NiPoint3&)` | 0x004A3E00 | 0x825C59D8 | | high | identical body |
| `NI2HK(float)` | 0x004A3E90 | 0x825C5B28 | `x * fBS2HkScale` | high | identical body |
| `fBS2HkScale` (float, set at runtime) | 0x011C582C (`g_fBS2HkScale`) | fBS2HkScaleSC_28 0x8407E2CC | value **1/7** per JIP (`kUnitConv = 0x3E124DD2`); 0 in the file | high (identity), medium (value: JIP only) | |
| `NiNode::UpdateDownwardPass` (user name) | 0x00A5DD70 | | refresh world transforms after moving the root | high | user name + JIP |

Reference sequence (from JIP `TESObjectREFR::SetPos`, documentation only): SetLocationOnReference(pos) → copy pos into 3D root local translate (+0x58 on PC NiAVObject: local transform translate) → if actor with charCtrl: charCtrl->SetPosition → `UpdateDownwardPass(root, updateData, 0)`.

### B. Heavy: queued teleport (cell loading, fast travel)
| Item | PC | Xbox | Notes | Conf. |
|---|---|---|---|---|
| `PlayerCharacter::HandlePositionPlayerRequest` | **0x0093BEA0** | 0x82CD2F10 | consumes `this+0x1EC pQueuedTargetLoc`; called first in Main::OnIdle_UpdatePlayer | **high**: identical assert string "AI: Invalid PositionPlayer request encountered…" |
| `PlayerCharacter::PositionPlayer` (interior, user name) | (user) | 0x82CD32D0 | | high |
| `PlayerCharacter::PositionPlayerExterior` | 0x0093CCE0 | 0x82CD3FE0 | branch when request.pWorld != 0 | medium-high |
| `PlayerCharacter::FastTravel` | 0x0093CDF0 | 0x82CD4110 | branch when fast-travel marker | medium-high |
| `bhkCharacterController::SetFallStartHeight(float)` | 0x00573F20 | 0x82705E90 | called with target z after positioning | medium-high |

PC `QueuedTargetLoc` layout (from 0x93BEA0): +0x00 pWorld, +0x04 pInterior, +0x08..0x1C location xyz + angle xyz, +0x20 bool, +0x24 pfnArrivalFunc, +0x28 arrival data, +0x2C furniture ref, +0x30 fast-travel marker. **Divergence:** the Xbox field order differs (the decompiler shows Location/Angle shuffled), so use the PC layout. Conf. medium.

## 2. Physics controller and velocity
| Item | PC | Xbox | Conf. |
|---|---|---|---|
| `bhkCharacterProxy::SetLinearVelocity(const hkVector4&)` | 0x0080FB00 (was the FID false positive `OnStatusTextChange`) | 0x82B29598 | high (body) |
| `hkpCharacterProxy::setLinearVelocity` -> **`m_velocity` @ hkpCharacterProxy+0x10** (hkVector4, Havok units/s) | 0x00CAC960 | 0x83344628 | high |
| `hkVector4::getZero` | 0x00458B20 | 0x825357F8 | high (call position x2) |
| `bhkCharacterController::QFallStartHeight` / `QHitDamage` / `ClearManifold` | 0x005D10F0 / 0x0087CEE0 / 0x00C6E030 | 0x8278DDB0 / 0x82BD51F0 / 0x832B3038 | high (same 4-call sequence at 2 PC sites) |
| Velocity in game units = m_velocity / fBS2HkScale (x7) | | | inferred |

| Item | PC | Xbox | Conf. |
|---|---|---|---|
| charCtrl access | `HighProcess+0x138` (NVSE/JIP: `actor->baseProcess(+0x68)`, processLevel(+0x28) ≤ 1) | MobileObject::GetCharController 0x82CC4A40 | NVSE/JIP |
| `bhkCharacterController::Move(MoveData&)` | 0x00C73170 | 0x832B5DE0 | medium-high (profiler strings "TtCharacter movement", "Ttupdate character state") |
| vtable bhkCharacterController | 0x010C49C4 (JIP) | | JIP only |
| Other Xbox APIs to locate (not yet on PC) | — | SetVelocityModifier 0x832B5808, SetCollisionBoxEnabled 0x832BA0E0, Jump 0x832B5630, SetWantState 0x82BE92E8, GetPosition 0x82B2CDE0 | TODO |

## 3. Disabling controls
| Item | PC | Xbox | Conf. |
|---|---|---|---|
| `PlayerCharacter::disabledControlFlags` | `+0x680` (byte) | | high (NVSE + both setters below) |
| `PlayerCharacter::SetControlsDisabled(bool bDisable, uint8 mask)` | **0x0095F530** | 0x82CFB578 | high |
| `PlayerCharacter::SetControlsDisabled(uint8 flags)` | 0x0095F590 | 0x82CFB608 | high |
| `DisablePlayerControls` script command execute | 0x005CAE30 (CommandInfo @ 0x01191838, opcode 0x1061) | Script::DisablePlayerControlsFunction | high |
| Mask bits | 1 Movement, 2 Look, 4 Pip-Boy, 8 Fight, 0x10 POV switch, 0x20 Rollover text, 0x40 Sneak (matches NVSE enum, command defaults = 1\|4\|8) | | high |

Notes added 2026-10-02 (plugin M2.3, Ghidra):
- 0x0095F530 is `__thiscall`, `ret 8`, and reads the mask as a **dword** (`OR EDX,[EBP+0xC]`), so pass it zero-extended.
- 0x0095F590 side effects: bit 1 set → `HUDMainMenu::UpdateVisibilityState(0xC)` (HUD hidden), cleared → `(1)`. Bit 8 → `FUN_008a6840(0)`. Bit 0x10 → clears `+0x64C` (forces 1st person) and updates the camera. Bit 0x40 → clears the sneak movement flag (0x400). A value other than 0xFF is masked with 0x7F.
- **`+0x680` is saved**: both player load paths (0x00958990 and 0x0095C0A0, callers of 0x0095F590) re-apply the saved byte through the setter. A plugin that sets bits must not leave them in a save it doesn't control. high.
- **No pre-save message, no co-save without an opcode base**: NVSE's `kMessage_SaveGame` is sent from 0x00847C45, after the .fos is written, and xNVSE skips the serialization callbacks of a plugin that has the default opcode base (`HandleSaveGame: plugin … has the default opcode base registered for serialization`). The usable pre-save point is `BGSSaveLoadGame::SaveGame` 0x00847850 (`__thiscall`, 1 arg, `ret 4`, writes the whole .fos). Its only xref is the `call` at 0x008505BB (`push edx; mov ecx,[0x011DDF38]; call`), in FUN_008503b0. JIP and xNVSE don't patch that site. high (Ghidra 2026-10-02; GOG bytes match).

Notes added 2026-10-03 (plugin M4.3b, Ghidra): other readers of `+0x680` that matter while a plugin holds bits:
- **Wait/Rest key**: `PlayerCharacter::Update` reads `GetControlState(Rest=0xF, pressed)` (0x0094238C), then `call 0x005A03D0` (returns the byte `+0x680`) at **0x0094239C**. Any non-zero flag skips Wait **silently**. Only after that come the checks that print a message: trespassing (vtbl +0x448), alarm (0x00953C80), underwater, hostiles nearby, in the air (char controller state 1/2, `sNoWaitInAir`), cell, radiation, taking health damage. Then `FUN_007054f0(0)` opens the Wait menu. 0x005A03D0 has two callers, this one and the `GetPlayerControlsDisabled` console command. high.
- **HUD after a menu closes**: 0x0070C4A0 re-picks the HUD visibility state; at **0x0070C982** it does `push 1; call 0x005A03F0` (`IsControlDisabled(mask)`, thiscall, `ret 4`) and passes 0xC (hidden) when Movement is set. So the HUD is re-hidden after each menu, not only by the setter.
- **Activate refused while Movement is disabled**: in PlayerCharacter::Update the Activate handler (control 5, 0x00942E3D) takes the crosshair ref (0x00703350) and at **0x00942F0C** calls `IsControlDisabled(1)`; if true it sets the refuse flag, and the handler goes to 0x00943378 (activate-nothing sound) instead of activating. Ghidra + GOG bytes 2026-10-03, high.
- `HUDMainMenu::UpdateVisibilityState(state)` **0x00771700**, cdecl, one arg (45 callers; 1 = normal, 0xC = controls disabled). It **re-tests Movement itself**: for state 1 with no menu, at 0x00771836 (→ recursive call with 0xC), and again for any final state other than 0x18/4/5 at 0x00771902 (→ 0xC, or 2 if FUN_00719AE0). Both are `push 1; mov ecx,[g_thePlayer]; call 0x005A03F0`. Flag 0x20 (rollover) is read at 0x00771972 (HUD mask 0x1FDFF instead of 0x1FFFF). high.

## 4. Hiding the player model
| Item | PC | Xbox | Conf. |
|---|---|---|---|
| `NiAVObject::m_flags` | +0x30 | m_uFlags +0x30 | high (JIP + Xbox PDB type) |
| AppCulled bit | `0x1` | NiAVObject::SetAppCulled 0x82529488 → SetBit(…, 1) | high |
| **Divergence** | NiAVObject transforms at +0x34 (local) / +0x68 (world) on PC (JIP) | +0x40 / +0x80 on Xbox (VMX alignment) | high: **never use Xbox offsets after +0x30 for Ni classes** |
| Player 3rd-person root | `TESObjectREFR::Get3D` 0x0043FCD0 (user) = `renderState(+0x64)->rootNode(+0x14)` | ?Get3D@TESObjectREFR@@ 0x825019C0 | high (vtable slot +0x1D0 of TESObjectREFR vtbl 0x102F55C) |
| `PlayerCharacter::Get3D()` (virtual, slot +0x1D0) | **0x00950B60** | 0x82CEA5D0 | high: returns 1st-person node `+0x694` if byte `+0x64A`==0 and the node is non-null, else 3rd-person. **Note: +0x64A, not NVSE's bThirdPerson +0x64C** |
| `PlayerCharacter::Get3D(bool firstPerson)` (user name `RetrieveRootNode`) | 0x00950BB0 | 0x82CEA630 | high |
| Player 1st-person root | `PlayerCharacter::playerNode +0x694` (NVSE) | | NVSE only |
| `PlayerCharacter::RetrieveRootNode` (user) | 0x00950BB0 | | user |
| `PlayerCharacter::Show1stPerson(bool)` | **0x00951A10** | 0x82CEB638 | high (Ghidra 2026-10-02): sets AppCulled on the 3rd-person root = `bShow1st` and on the 1st-person root = `!bShow1st` (scope path aside), so it **rewrites the 3rd-person bit on every view switch**; a hide must be re-applied each frame |
| AppCulled vs. transforms | `NiNode::UpdateDownwardPass` 0x00A5DD70 | | high: a culled node only ORs a culled flag into the update data passed to its children; their world transforms are still updated (camera pivot / bones keep working) |
| Water reflection un-cull (high-detail reflections) | TESWater 0x004EAA00 sets `+0x9C`=1 when it runs in 1st person (`+0x64A`==0) and culls Get3D(); 0x004EB540, after the reflection pass, if `+0x9C` and `+0x64A`!=0, calls `SetAppCulled(0)` (0x00450F90) at **0x004EB75A** on Get3D() = 3rd-person root. `+0x9C` is cleared only by 0x004E1650. Gated by `bForceHighDetailReflections:Water` (0x011C7C00; on in Ultra). Shows a hidden body for the rest of the frame whenever water is drawn | | high (Ghidra 2026-10-03, GOG bytes match) |

## 5. State predicates (partial)
| State | Source | Conf. |
|---|---|---|
| in dialogue | `g_bInDialog` 0x11DEA2C (P1) | high |
| VATS | `pVATS(0x11F2250)+0x08 == 4` playback; `PlayerCharacter::IsInSlowMo` 0x007D1360 | high |
| dead | `Actor::IsDead(bool notEssential)` **0x008844F0** (vtbl +0x22C) = lifeState in {1,2} (+6 ESSENTIAL_DOWN if !notEssential); `Actor::GetLifeState` 0x004F8960 (user) reads **+0x108** | high |
| sitting / furniture | `Actor::GetSitSleepState` **0x008D8800** (vtbl +0x214) reads **+0x1AC** | high |
| swimming | `Actor::GetMovementFlags` 0x008846E0 (user) bit 11 (NVSE) | medium (bit from NVSE) |
| 3rd person | `PlayerCharacter::bThirdPerson +0x64C` (NVSE) | NVSE only |

## Left for later
- PC equivalents of bhkCharacterController::SetVelocityModifier / SetCollisionBoxEnabled / Jump (only needed if the plugin drives the capsule instead of teleporting it).
- Semantics of +0x64A vs +0x64C (render-1st-person vs camera-3rd-person flag): revisit in P4 (camera).

## Control flags vs. NPC combat (M10.1, Ghidra 2026-10-04)
- **Movement (1)**: Actor virtual **0x008A03A0** (PlayerCharacter vtable +0x430; also at 0x01084684 / 0x01086E9C / 0x010874DC) reads the actor's combat target and, at **0x008A04B2** (`push 1; mov ecx,[0x011DEA3C]; call 0x005A03F0`), skips the combat update when the target is the player with Movement disabled. high
- **Fight (8)**: AI procedures **0x008ECF20** (0x008ECF78) and **0x00918120** (0x0091815D) give up on the player as target (vt+0x294) while Fight is disabled. high
- `PlayerCharacter::IsImmobile` (vt+0x4B4) = **0x009397E0** = `IsControlDisabled(1)`. Read through vt+0x4B4 by 14 sites (CanKnockDown 0x008845A0, Kill, Move 0x0092F260, 0x00762160, 0x00881860, 0x00888B50, 0x0088B880, 0x008B01C0, 0x008F4020, 0x00902290, 0x00902C80, 0x00912B00, …); roles not reversed. medium
