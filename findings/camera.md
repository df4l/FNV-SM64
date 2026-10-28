# P4: Camera (DONE, 2026-10-01)

PC = FalloutNV.exe 1.4.0.525. Xbox = Fallout_Debug.exe. "(user)" = name set by the user, kept.
FNV has **no `PlayerCamera` class** (that's Skyrim). The camera is driven by `PlayerCharacter::UpdateCamera` + statics, and it ends up in the scene graph's **camera root node**.

## 1. Objects
| Item | PC | Xbox | Conf. | How verified |
|---|---|---|---|---|
| World scene graph `Main::spWorldRoot` (user `g_SceneGraph`) | 0x011DEB7C | 0x84098358 | high | P1 |
| `Main::WorldRootCamera()` -> NiCamera* (user `GetWorldSceneGraph`) | 0x00524C90 | 0x8268AAA0 | high | P1 |
| `BSSceneGraph::GetCamera` (user `GetCamera___`) | 0x006629F0 | 0x8253C018 | high | P1 |
| **`BSSceneGraph::GetCameraRoot`** = child 0 of the scene graph (`NiNode::GetAtChecked(0)` 0x0045BC00) | **0x00558310** | 0x826DACC8 | high | body + 3 uses at the end of UpdateCamera |
| `NiCamera::GetWorldDirection` = world rotate **column 0** (camera looks along local +X) | 0x0045BBA0 | 0x82539CE8 | high | body: `GetCol(this+0x68, 0)` |
| `NiCamera::GetWorldUpVector` = column 1 | 0x004E9C10 | 0x82629F70 | high | body |
| `NiCamera::LookAtWorldPoint(pt, up)` | 0x00A701B0 | 0x8393F1E8 | high | call position (chase camera, UNIT_Z 0x11A9484) |
| `NiCamera::GetFrustum` (user), frustum @ NiCamera+0xEC (NVSE) | (user) | | medium | |
| **NiAVObject world rotate @ +0x68, world translate @ +0x8C (PC)** | | Xbox +0x80/+0xA4 | high | `GetWorldDirection` uses this+0x68 + JIP |

Camera forward (game units, Z up): `(R[0][0], R[1][0], R[2][0])` of the row-major NiMatrix3 at camera+0x68, i.e. floats at +0x68, +0x74, +0x80. Camera position = floats at +0x8C/+0x90/+0x94. Float layout inferred (Gamebryo row-major), conf. medium. Horizontal look for SM64 `camLookX/Z` = normalize(forward.x, forward.y).

## 2. `PlayerCharacter::UpdateCamera` (user) 0x0094AE40 = Xbox 0x82CE3AA0, conf. high
Statics (labels added): `g_fCamHeading` 0x11E076C (= player heading this frame), `g_fCamLooking` 0x11E0764 (pitch), `g_chaseCamera` NiCamera 0x11E0C20, `g_chaseCameraPos` 0x11E0808, `g_vanityModePos` 0x11E0B58 (x pitch-ofs, **y = 3rd-person distance**, z yaw-ofs), `g_bVanityMode` 0x11E07B8, `g_bAutoVanityMode` 0x11E07B9, `g_p1stPersonCamera` 0x11E07D0, `kg_Camera3rd` (user, = p3rdPersonCamera) 0x11E07D4, `g_p1stPersonBip` 0x11E07D8, `g_fDeadEyeFade` 0x11A3B64.

Pipeline:
1. FOV: `SceneGraph::SetCameraFOV(worldFOV)` + `BSShaderManager::SetFOV` (3rd person uses f3rdPersonFOV).
2. VATS playback + bWant3rdPerson → chase camera looks at `VATS::GetTargetPos` (0x9C8AC0).
3. **3rd person** (`b3rdPerson || bVanityMode || bWant3rdPerson != b3rdPerson`): pivot = `Get3D(false)` world pos + fEyeHeight×scale + (1st − 3rd camera node Δz); rot = Z(heading+vanity.z)·X(looking+vanity.x); offset by vanityModePos.y and the over-shoulder GMSTs (fOverShoulderPosX/Z, fOverShoulderOffsetPoint*, …); `PlayerCharacter::Setup3rdPersonCameraCaster` (0x95EDF0) + `SetThirdPersonCameraPosition` (user, = UpdateChaseCamera: camera collision); `LookAtWorldPoint`.
4. **1st person**: rot = Z(heading)·X(looking); pos = `g_p1stPersonCamera` world translate (+ head-bone blending).
5. Camera shake, then **`GetCameraRoot()->SetTranslate(pos); ->SetLocalRotate(rot) (user); ->Update()`**.
6. Audio listener (`BSAudio::PositionListener/OrientListener/SetListenerUnderwater` 0xAD78B0/0xAD7940/0xAD7990), Havok camera, grass/LOD streaming around the camera.

## 3. 1st / 3rd person state (resolves the P2 question)
| Field | PC | Xbox | Conf. |
|---|---|---|---|
| `b3rdPerson` (current view) | **+0x64A** | +0x65A | high: UpdateCamera tests and Get3D() |
| `bActually3rdPerson` | +0x64B | +0x65B | medium (adjacency; read once in Update) |
| `bWant3rdPerson` (requested view; **NVSE calls it bThirdPerson**) | **+0x64C** | +0x65C | high: UpdateCamera + SetFirstPerson |
| saved/temp 3rd-person (ForceTemp3rdPerson) | +0x648 / +0x649 | | medium (Update writes) |
| **`PlayerCharacter::SetFirstPerson(bool bFirst)`** | **0x00950110** | 0x82CE98A8 | high: line-by-line match |
| `PlayerCharacter::Show1stPerson(bool)` | (user) | 0x82CEB638 | high |
| `g_bVATSForce1stPerson` / `g_bVATSReturnPlayerTo1stPerson` | 0x011F21D0 / 0x011F21D1 | | high |
| POV toggle input | inline in `PlayerCharacter::Update` (ControlCode 0x0D TogglePOV) | | medium |

## 4. Mouse / stick look
- Heading/pitch come from the player ref (`GetHeading`, `MobileObject::GetLooking`) and are copied into g_fCamHeading/g_fCamLooking each UpdateCamera.
- The look input is integrated in `PlayerCharacter::Update` (right stick axis 9 with deadzone 8689; mouse via OSInputGlobals; JIP hooks `GetMouseMovement` at 0xA239E0, not verified here).

## 5. Takeover options for SM64 (documentation only, see open-questions D9)
- **A. Follow by proxy (SA approach):** keep the player ref at Mario's interpolated position (MobileObject::SetPosition) and force 3rd person (SetFirstPerson(false)). The vanilla 3rd-person camera then orbits Mario with the vanilla mouse/stick look. Mario's facing is independent: do not copy Mario's yaw into the player heading, or the camera will spin with him.
- **B. Override:** post-hook `PlayerCharacter::UpdateCamera` and rewrite `GetCameraRoot()` translate/rotate (then `Update`). Full control (SM64-style lakitu cam), but the audio listener / LOD were already computed from the vanilla pose that frame.
- Either way, Mario's `camLookX/Z` come from the world camera forward (column 0) projected on XY.

## 6. Zoom distance limits (RE side-task, 2026-10-03)
The 3rd-person zoom distance is `g_vanityModePos.y` (0x011E0B5C, float). Clamps:
| GMST | Setting object | Exe default | Used at | Conf. |
|---|---|---|---|---|
| `fChaseCameraMax` | 0x011CD568 | 120 | PlayerCharacter::Update wheel clamp 0x00945BB4; SetThirdPersonCameraPosition 0x0094A3C5 (camera distance cap) | high |
| `fVanityModeWheelMax` | 0x011CDE14 | 600 | replaces fChaseCameraMax when `g_bVanityMode` (0x011E07B8) is set (0x00945BD1, 0x0094A3E2); also the zoom-blend ratio 0x0094A147 | high |
| `fVanityModeWheelMin` | 0x011CDA94 | 30 | zoom below it → `SetFirstPerson(true)` (0x00945B9D) | high |
| `fVanityModeWheelDefault` | 0x011CCF2C | 60 | FUN_00938180 | medium |
| `fVanityModeWheelDeadMin` | 0x011CD614 | ? | FUN_009445b0 / SetFirstPerson | medium |
Static initialisers at 0x00F5B2E0..0x00F5B3D0 (`Setting(name, default)` 0x0040E0B0), so the ESM can override these. The plugin raises `fChaseCameraMax` through NVSE `GameSettingCollection::GetGameSetting` while Mario is spawned and restores it on despawn.

### Gamepad zoom (2026-10-03, Ghidra, high)
- In gamepad mode (FUN_004B71D0 true), FUN_009445B0 zooms **only while `0x011E07C1` is set**. PlayerCharacter::Update sets it at 0x00942C9A when the POV control is held (unless POV switch is disabled or a menu is open), and also enters vanity mode (`g_bVanityMode` = 1, saves the zoom to 0x011E0BBC, `UpdateVisibilityState(0x14)`). Then right-stick Y (`GetJoystickAxis(0, 0xA)`, deadzone 0x21F1) becomes the wheel delta and goes through the same zoom code and clamp as the mouse wheel (0x00945A73..0x00945BFE). While held, the clamp is `fVanityModeWheelMax`. Otherwise the right stick X (axis 9) is look input.
- Release (0x00942D0D, POV not held): vanity off, `UpdateVisibilityState(1)`; a short press with no zoom change toggles 1st/3rd person; the vanity pitch/yaw offsets (0x011E0B58/0x011E0B60) are zeroed and `0x011E07C1` is cleared. The zoom value itself is kept, so the next frame's clamp uses `fChaseCameraMax`.
- The 3rd-person zoom `0x011E0B5C` is **written to the save** (load 0x00956F70 / 0x00955620 for save version ≥ 0x7A; save 0x009590F0 / 0x0095A3B0).
- **Game data value**: `fVanityModeWheelMax` is most likely **250** in game (the ESM overriding the exe's 600): pad zoom stopped at exactly 250 (F10, 2026-10-03). The M4.1c F10 line prints the GMST itself, to confirm.
