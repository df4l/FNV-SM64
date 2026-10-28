# addresses.md

Confidence: **high** = ≥2 independent signals agree; **medium** = 1 strong signal (e.g. an NVSE address confirmed to be a function or instruction in Ghidra); **low** = single weak signal.
"NVSE" means the address comes from xNVSE source (commit 0ccd23ad), i.e. it is used by a widely deployed plugin against 1.4.0.525.

| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| Main loop hook site (call in oldWinMain, before Sleep) | 0x0086B386 (`E8 25 11 BD FF` → call 0x0043C4B0) | — | medium | NVSE `kMainLoopHookPatchAddr`; Ghidra read_memory shows a 5-byte `call` at that address |
| `isMenuMode()` | 0x00702360 | — | medium | NVSE `CdeclCall<bool>(0x702360)`; Ghidra has a function there, pre-named `isMenuMode` (0x702360–0x702390) |
| `TESObjectREFR::Set3D(NiNode*, bool)` | 0x0094EB40 | — | medium | NVSE `DEFINE_MEMBER_FN(Set3D, 0x0094EB40)`; Ghidra function pre-named `Set3D` |
| `PlayerCharacter::UpdateCamera` | 0x0094AE40 | TBD | low (unverified by me) | pre-existing Ghidra name; NVSE declares `PlayerCharacter::UpdateCamera(bool,bool)` |
| `PlayerCharacter::RetrieveRootNode` | 0x00950BB0 | TBD | low (unverified by me) | pre-existing Ghidra name |
| `g_thePlayer` (PlayerCharacter**) | 0x011DEA3C | — | NVSE, not yet checked | NVSE GameObjects.cpp |
| `appTimer` (BSTimer; NVSE `g_timeGlobal`) | 0x011F6394 | `appTimer` 0x840B796C | high | ECX for BSTimer::Update/GetSeconds in OnIdle_UpdateTimer (disasm) + NVSE |
| `g_OSInputGlobals` (OSInputGlobals**) | 0x011F35CC | — | NVSE, not yet checked | NVSE GameOSDepend.cpp |
| `g_osGlobals` (OSGlobals**) | 0x011DEA0C | — | NVSE, not yet checked | NVSE GameOSDepend.cpp |
| StartMenu* | 0x011DAAC0 | — | high | NVSE/JIP + used by isInStartMenu (user) 0x0070EDF0 |
| `VATS::GetPlayerUpdateMult` (this = pVATS 0x011F2250) | 0x009C8CC0 | 0x82D9D940 | high | NVSE usage + same role in OnIdle_UpdatePlayer. Renamed in Ghidra |

## Added in P1 (session 1). Details in findings/main-loop.md

| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `Main::OnIdle` (user name `FonctionDinteret2`) | 0x0086E650 | ?OnIdle@Main@@ 0x82BBE778 | high | call sequence 1:1 + guards. Comment only |
| `Main::Swap` (user name `FonctionDinteret`) | 0x0086FF70 | ?Swap@Main@@ 0x82BC19B8 | medium | position only. Comment only |
| `Main::OnIdle_UpdateTimer` | 0x0086F260 | 0x82BC05F8 | high | position + body. Renamed |
| `Main::OnIdle_PollControls` | 0x0086F390 | 0x82BC07D8 | high | position + body. Renamed |
| `Main::OnIdle_UpdatePlayer` | 0x0086F940 | 0x82BC10A8 | high | position + body. Renamed |
| `Main::OnIdle_UpdateImageSpace(bool)` | 0x0086FD90 | 0x82BC17D0 | medium-high | position + arg. Renamed |
| `Main::OnIdle_UpdateCurrentGridCell` | 0x0086FBE0 | 0x82BC1478 | medium | position. Renamed |
| `Main::OnIdle_UpdateAnimationsAndEffects` | 0x0086FC60 | 0x82BC1588 | medium | position. Renamed |
| `Main::OnIdle_DoInterfaceIdle` | 0x0086FD70 | 0x82BC1758 | high | two call sites match. Renamed |
| `Main::PostSwapProcess` | 0x008705D0 | 0x82BC21C0 | medium-high | position. Renamed |
| `BSTimer::GetSeconds` | 0x0084D030 | 0x8252D260 | high | ECX=appTimer + body (+0xC). Renamed |
| `BSTimer::Update` | 0x00771520 | 0x82A3DA08 | high | ECX=appTimer + position. Renamed |
| `bhkWorld::SetDeltaTime(float,bool,bool)` | 0x00C66760 | 0x832A59C8 | high | position + args. Renamed |
| `Interface::IsPipboyOpening` | 0x00709BC0 | 0x829B5130 | high | 3 call-site patterns. Renamed |
| `NiDX9Renderer::BeginOffScreenFrame` / `EndOffScreenFrame` | 0x0086BA10 / 0x0086BA90 | NiXenonRenderer:: 0x831781A0 / 0x83178228 | high | name strings + frame state. Renamed |
| `PlayerCharacter::HandlePositionPlayerRequest` | 0x0093BEA0 | 0x82CD2F10 | high | identical assert string + position. Renamed (P2) |
| `Main::WorldRootCamera()` (user name `GetWorldSceneGraph`) | 0x00524C90 | 0x8268AAA0 | high | identical body. Plate comment added |
| `Main::spWorldRoot` NiPointer<SceneGraph> (user `g_SceneGraph`) | 0x011DEB7C | 0x84098358 | high | same |
| `BSSceneGraph::GetCamera` (user `GetCamera___`) | 0x006629F0 | 0x8253C018 | high | same |
| `Main::bInMenuMode` (user `g_bIsMenuMode`) | 0x011DEA2B | 0x84098203 | high | disasm in OnIdle_UpdateTimer |
| `Main::fFrameAnimTime` | 0x011DEA30 | 0x84098208 | high | body of OnIdle_UpdateTimer |
| `Main::iFrameCounter` | 0x011A2FE0 | 0x84041238 | medium | ++ at same position |
| LoadingMenu* (used by isInLoadingScreen, user) | 0x011DA0C0 | — | high | disasm 0x00705EA0 |
| `bhkWorld::fTauRatio` | 0x011AFE60 | 0x8404CD50 | high | OnIdle_UpdateTimer |
| `cSkipThreadQueue` | 0x011E07A8 | 0x8409A2DC | high | OnIdle_UpdatePlayer |
| Probable reset-notify callback | 0x00872570 (registered by 0x0086BAE0) | — (PC only) | low-medium | Gamebryo API shape. Not renamed, SM64-UNSURE |
| Shared getter `return *(this+8)` (COMDAT-folded: TESForm::GetFlags / VATS::GetMode ...) | 0x0044DDC0 | — | high | do not rename to a single meaning |

## Added in P2. Details in findings/player.md
| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `bhkCharacterController::SetPosition(NiPoint3)` | 0x005620E0 | 0x826EC628 | high | identical body. Renamed `_NiPoint3` |
| `bhkCharacterController::SetPosition(hkVector4)` | 0x00C6E390 | 0x832B5778 | high | body. Renamed `_hkVector4` |
| `bhkCharacterController::Move` | 0x00C73170 | 0x832B5DE0 | medium-high | profiler strings |
| `bhkCharacterController::SetFallStartHeight` | 0x00573F20 | 0x82705E90 | medium-high | call site in HandlePositionPlayerRequest |
| `hkpCharacterProxy::setPosition` | 0x00CAC920 | 0x83344608 | high | body |
| `hkpShapePhantom::setPosition` | 0x00C9E990 | 0x83328910 | high | body (+0xE0) |
| `hkpPhantom::updateBroadPhase` | 0x00D07EC0 | 0x83439528 | high | |
| `NI2HK(hkVector4&, NiPoint3&)` / `NI2HK(float)` | 0x004A3E00 / 0x004A3E90 | 0x825C59D8 / 0x825C5B28 | high | identical bodies |
| `fBS2HkScale` | 0x011C582C | 0x8407E2CC | high (value 1/7: JIP) | |
| `PlayerCharacter::PositionPlayerExterior` | 0x0093CCE0 | 0x82CD3FE0 | medium-high | branch structure |
| `PlayerCharacter::FastTravel` | 0x0093CDF0 | 0x82CD4110 | medium-high | branch structure |
| `PlayerCharacter::SetControlsDisabled(bool, uint8)` | 0x0095F530 | 0x82CFB578 | high | body (+0x680) + command |
| `PlayerCharacter::SetControlsDisabled(uint8)` | 0x0095F590 | 0x82CFB608 | high | |
| `BGSSaveLoadGame::SaveGame(arg)` | 0x00847850 | | high | `ret 4`, contains NVSE's post-save hook 0x00847C45 (player.md §3) |
| its only call site (`call`, ECX = [0x011DDF38]) | 0x008505BB | | high | Ghidra xref; GOG bytes checked |
| `DisablePlayerControls` execute / CommandInfo | 0x005CAE30 / 0x01191838 | Script::DisablePlayerControlsFunction 0x82784AD0 | high | string ptr in command table |
| `TESObjectREFR::SetLocationOnReference` (user) | 0x00575830 | 0x82708228 | high | user + JIP |
| `NiNode::UpdateDownwardPass` (user) | 0x00A5DD70 | | high | user + JIP |

| `MobileObject::SetPosition` (user, virtual) | 0x00931620 | 0x82CC5D88 | high | call-for-call match. **Recommended per-frame teleport** |
| `PlayerCharacter::Get3D()` | 0x00950B60 | 0x82CEA5D0 | high | vtable slot +0x1D0 + body. Renamed |
| `PlayerCharacter::Get3D(bool)` (user `RetrieveRootNode`) | 0x00950BB0 | 0x82CEA630 | high | body |
| `TESObjectREFR::Get3D` (user) | 0x0043FCD0 | 0x825019C0 | high | vtable slot +0x1D0 of 0x102F55C |
| `bhkCharacterProxy::SetLinearVelocity` | 0x0080FB00 | 0x82B29598 | high | body. Replaced FID false positive `OnStatusTextChange` (src=ANALYSIS) |
| `hkpCharacterProxy::setLinearVelocity` (m_velocity +0x10) | 0x00CAC960 | 0x83344628 | high | |
| `hkVector4::getZero` | 0x00458B20 | 0x825357F8 | high | |
| `bhkCharacterController::QFallStartHeight` / `QHitDamage` / `ClearManifold` | 0x005D10F0 / 0x0087CEE0 / 0x00C6E030 | 0x8278DDB0 / 0x82BD51F0 / 0x832B3038 | high | |
| `Actor::IsDead(bool)` | 0x008844F0 | 0x82BE0908 | high | vtbl +0x22C + body |
| `Actor::GetLifeState` (user) -> +0x108 | 0x004F8960 | 0x824F2EB0 | high | disasm |
| `Actor::GetSitSleepState` -> +0x1AC | 0x008D8800 | 0x82BD6EE8 | high | vtbl +0x214 (6 actor vtables) |

## Added in P3. Details in findings/input.md
| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `PlayerCharacter::Update(float)` (**function created**) | 0x0093E860 | 0x82CD5BF8 | high | vtbl 0x108AA3C+0x2F8, NVSE hook sites + stick reads inside |
| `OSInputGlobals::GetControlState` (user `GetControlState`) | 0x00A24660 | Controls::GetUserAction 0x82F981E0 | high | NVSE detours |
| `OSInputGlobals::Poll` | 0x00A23010 | Controls::Poll 0x82F97870 | high (role) | XInputGetState/DI GetDeviceState |
| `OSInputGlobals::GetJoystickAxis` | 0x00A23390 | Controls::GetJoystickAxis 0x82CDBFE8 | high | body + 41 callers with (0, 7..10) |
| `OSInputGlobals::GetButtonState` (+ Keyboard 0xA24180 / Mouse 0xA23A50 / Gamepad 0xA23E10 / Joystick 0xA234C0 / POV 0xA235F0) | 0x00A24560 | — (PC-only) | medium | dispatch body |
| `OSInputGlobals::maybe_ForceGamepadButton` | 0x00A23D40 | — | low-medium | |
| `g_xinputState` / `g_xinputStatePrev` / `g_bGamepadActive` | 0x011F35A8 / 0x011F35B8 / 0x011F35C8 | — | high | Poll body |
| XInput deadzones in PlayerCharacter::Update | 7849 (L), 8689 (R) @0x941156.. | — | high | XInput SDK constants |

## Added in P4. Details in findings/camera.md
| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `PlayerCharacter::UpdateCamera` (user) | 0x0094AE40 | 0x82CE3AA0 | high | global/branch order. Plate comment |
| `BSSceneGraph::GetCameraRoot` | 0x00558310 | 0x826DACC8 | high | body (child 0) + use |
| `NiCamera::GetWorldDirection` / `GetWorldUpVector` | 0x0045BBA0 / 0x004E9C10 | 0x82539CE8 / 0x82629F70 | high | GetCol(this+0x68, 0/1) |
| `NiCamera::LookAtWorldPoint` | 0x00A701B0 | 0x8393F1E8 | high | |
| `PlayerCharacter::SetFirstPerson(bool)` | 0x00950110 | 0x82CE98A8 | high | line-by-line |
| `PlayerCharacter::Setup3rdPersonCameraCaster` | 0x0095EDF0 | 0x82CFAE30 | medium-high | position |
| `PlayerCharacter::GetBiped` | 0x00950B00 | 0x82CEA548 | medium-high | position |
| `VATS::GetTargetPos` | 0x009C8AC0 | 0x82D9D658 | medium-high | position |
| `MobileObject::IsInDialoguewithPlayer` | 0x00933840 | 0x82CC8A58 | medium-high | position |
| `BSAudio::PositionListener` / `OrientListener` / `SetListenerUnderwater` | 0x00AD78B0 / 0x00AD7940 / 0x00AD7990 | 0x82FCAF48 / 0x82FCB038 / 0x82FCB0A8 | medium-high | position (P8 audio) |
| `Actor::IsPointDeepUnderWater` | 0x00885560 | 0x82BE2200 | medium-high | position |
| `RenderState::waterLevel` getter (ref water height, game units, −FLT_MAX none) / `bhkCharacterController` water-height setter (NI2HK copy to +0x53C) | 0x0057B0A0 / 0x0057B1D0 | — | high | position (M10.2, gameplay.md §6) |
| statics: g_fCamHeading / g_fCamLooking / g_chaseCamera / g_chaseCameraPos / g_vanityModePos | 0x11E076C / 0x11E0764 / 0x11E0C20 / 0x11E0808 / 0x11E0B58 | fHeading / fLooking / chaseCamera / chaseCameraPos / vanityModePos | high | order in UpdateCamera |
| g_vanityModePos.y = 3rd-person zoom distance; GMST fChaseCameraMax caps it (fVanityModeWheelMax in vanity mode) | 0x11E0B5C; fChaseCameraMax obj 0x11CD568 | | high | camera.md §6, Ghidra 2026-10-03 |
| g_bVanityMode / g_bAutoVanityMode / g_p1stPersonCamera / kg_Camera3rd (user) / g_p1stPersonBip | 0x11E07B8 / 0x11E07B9 / 0x11E07D0 / 0x11E07D4 / 0x11E07D8 | | high | |
| g_bVATSForce1stPerson / g_bVATSReturnPlayerTo1stPerson | 0x11F21D0 / 0x11F21D1 | 0x840ABD30 / 0x840ABD31 | high | SetFirstPerson |

## Added in P5. Details in findings/collision.md
| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `hkPackedNiTriStripsShape::getChildShape` | 0x00CA3C40 | 0x833312A8 | high | container vtable slot +0x14 + body (float verts on PC) |
| `hkPackedNiTriStripsShape::getNextKey` / `getCollisionFilterInfo` / `getAabbImpl` / `getType` (=0x17) | 0x00CA35D0 / 0x00CA3670 / 0x00CA37E0 / 0x00CA3550 | 0x83331170 / 0x83331588 / 0x833319A8 / 0x83331150 | high | vtable slot order |
| vtables (RTTI): hkPackedNiTriStripsShape 0x10C755C (+container 0x10C753C), bhkPackedNiTriStripsShape 0x10C761C, hkNiTriStripsShape 0x10C75C4, hkpMoppBvTreeShape 0x10D0DAC, bhkMoppBvTreeShape 0x10CA24C, hkpListShape 0x10CC3D8, hkpBoxShape 0x10C70B4, hkpConvexVerticesShape 0x10D0FC4, hkpCapsuleShape 0x10CE25C, hkpSphereShape 0x10CB5AC, hkpConvexTransformShape 0x10D102C, hkpTransformShape 0x10CC41C, hkpExtendedMeshShape 0x10D0ECC, hkpCompressedMeshShape 0x10EBA5C, bhkRigidBody 0x10301B4, hkpRigidBody 0x10C7888, bhkWorldM 0x10C69F4, hkpWorld 0x10C6714, TESObjectLAND 0x102DCD4 | (see list) | | high | RTTI TypeDescriptor → COL → vtable. Labelled `vtbl_*` |
| `TESObjectLAND::LoadedLandData` fields: +0x00 block nodes, +0x04 vertex arrays (4×17×17), +0x94 spLandRB (**null on PC**), +0x98/+0x9C cell X/Y, +0xA0 base height | (offsets) | `TESObjectLAND::LoadedLandData` | high (layout), medium (vertex frame) | JIP GameForms.h + Xbox type; +0x94 null and cell X/Y seen in game (M5.1, 2026-10-03). Terrain collision = per-block heightfield in ExtraCellMopp (collision.md §5) |

## Added in P6. Details in findings/render.md
| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `NiTriShapeData::NiTriShapeData` | 0x00A7B630 | 0x83953890 | high | JIP + vtable ref |
| `NiTriShapeDynamicData::NiTriShapeDynamicData` | 0x00A816A0 | 0x83960380 | high | body (active counts @+0x58/+0x5A) |
| `NiTriShape::NiTriShape` | 0x00A74480 | | high | JIP + vtable ref |
| `BSShaderNoLightingProperty` ctor | 0x00B6FC90 | | high | JIP |
| `NiPixelData::NiPixelData(w,h,fmt,mips,faces)` | 0x00A7C190 | 0x83955030 | high | body |
| `NiSourceTexture::Create(NiPixelData*, name, FormatPrefs&)` | 0x00A5FF10 | 0x8391B060 | high | body (+0x3C pixelData) |
| `NiSourceTexture::Create(NiFile*…)` / `Create(path…)` / ctor | 0x00A5FE30 / 0x00A5FD70 / 0x00A5FC10 | 0x8391ADD8 / 0x8391AC58 | high | |
| `NiAVObject::UpdateProperties` | 0x00A5A040 | | medium-high | body |
| `BSShaderManager::maybe_InitGeometryRecurse` | 0x00B57BD0 | | high | JIP AssignGeometryProps; disasm 2026-10-02: cdecl (obj, a, b, c), recurses NiNode children, plain RET |
| `NiPixelFormat::RGBA32` (+ PAL4/PALA4/PAL8/PALA8/RGB24) | 0x011AA2A0 (0x11AA180/1C8/210/258/2E8) | 0x8412FC18 | high | alpha/no-alpha test fns + Xbox stride |
| vtables NiPixelData / NiSourceTexture / NiTriShape / NiTriShapeData / NiTriShapeDynamicData / NiTexturingProperty / NiVertexColorProperty / NiMaterialProperty / NiAlphaProperty / NiNode | 0x109DBB4 / 0x109B9EC / 0x109D454 / 0x109DAC4 / 0x109E32C / 0x109C514 / 0x109B72C / 0x109D6C4 / 0x10162DC / 0x109B5AC | | high | RTTI |

## Added in P7. Details in findings/scale.md
| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| units-per-metre constant 69.99125671386719 (double) | 0x010120E8 | | high | .rdata value + static init |
| `fHk2BSScale` = 6.99913 (runtime) / its initializer | 0x011C5880 / 0x00F3D3B0 | fHk2BSScaleSC | high | disasm |
| `fBS2HkScale` initializer | 0x00F3D3D0 | | high | disasm (1/fHk2BSScale) |
| `GetZAngleFromVector` / `ClampAngle` / unclamped variant | 0x004B13C0 / 0x004B1480 / 0x004B1550 | 0x825E05E0 / 0x825E06F0 | high | identical body (3π/2, π/2 constants) |

## Xbox reference anchors (IDA, Fallout_Debug.exe), PC counterparts to find

| Xbox symbol | Xbox address | Use |
|---|---|---|
| `Main::GetWorldSceneGraph` | 0x8253C048 | scene graph / world camera |
| `Main::WorldRootNode` | 0x8262B528 | attach point for the Mario mesh |
| `Main::WorldRootCamera` | 0x8268AAA0 | camera orientation for stick input |
| `Main::QFrameAnimTime` | 0x8252D3D8 | frame time |
| `PlayerCharacter::Is3rdPerson` | 0x8262B518 | camera mode |
| `bhkCharacterController::SetPosition(const NiPoint3&)` | 0x826EC628 | teleport the player physics |
| `NiTriShapeDynamicData` (vtable methods) | 0x839604A0.. | dynamic mesh rendering |
| `hkpMoppBvTreeShape::getShapeCollection` | 0x8281C490 | collision extraction |
| `XInputManager::GetPad` / `ButtonState` | 0x829BAE00 / 0x829C4F60 | Xbox-only input path; PC differs (DirectInput + XInput in OSInputGlobals) |

## P9: Gameplay (2026-10-01). Details in findings/gameplay.md

| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `Actor::DoDamage(float hp, float fatigue, Actor*)` | 0x0089D6F0 (vt+0x338) | 0x82BFF970 (vt+0x334) | high | vtable index (+4 shift) + JIP `DamageHealthAndFatigue` + body |
| `Actor::Kill(Actor* killer, float)` | 0x0089D900 | 0x82BFFC58 | high | shared strings (MUSDeath, "%.20s is dead!", "was killed by") + call graph |
| `Actor::HandleHealthDamage(Actor*, float)` | 0x00898650 (vt+0x4B8) | 0x82BF9B88 (vt+0x4B0) | high | 1:1 body (barks 0.5/0.25, Kill/QueueActorKill) + JIP `DoHealthDamage` |
| `PlayerCharacter::HandleHealthDamage` | 0x009508F0 | — | high | PC vtable override of vt+0x4B8 |
| `Actor::DamageModActorValue(av, float, Actor*)` | 0x00881130 (vt+0x3AC) | 0x82BDC3E8 (vt+0x3A8) | high | vtable + body (float arg) + JIP `DamageActorValue` + Cmd_DamageActorValue |
| `Actor::DamageModActorValue_int` | 0x00880FB0 (vt+0x3B0) | 0x82BDC1C8 (vt+0x3AC) | high | vtable + body (int arg) |
| `PlayerCharacter::DamageModActorValue` / `_int` | 0x0093B7A0 / 0x0093B3A0 | PlayerCharacter overrides | high | vtable |
| `Actor::TempModActorValue` (float) | 0x00880AD0 (vt+0x39C) | vt+0x398 | medium-high | vtable alignment + body |
| `Actor::PermanentModActorValue_int` | 0x00880C70 (vt+0x3A8) | vt+0x3A4 | medium-high | vtable alignment + body |
| `Actor::GetHealthPercentage` | 0x00893590 | 0x82BF3898 | high | 1:1 body + GetHealthPercentage condition |
| `Actor::IsSwimming` (byte +0x14D) | 0x005A2030 | 0x8274ACD0 (+0x15D) | high | 1:1 body + IsSwimming condition |
| `TESObjectREFR::DamageObject(float, bool)` | 0x00579220 (REFR vt+0x144) | 0x8270D280 (vt+0x140) | high | 1:1 body + DamageObject command |
| `BGSDestructibleObjectForm::DamageObject` | 0x00475B20 | 0x82579038 | medium-high | call position in REFR::DamageObject |
| `TESForm::GetDestroyed` | 0x00477BA0 | 0x8257CC88 | medium-high | call position + GetDestroyed condition |
| `TESForm::GetDestructible` | 0x00452370 | 0x8252B4E8 | medium | call position |
| `AILinearTaskThreadManager::ShouldQueue3DTask` | 0x008C7AA0 | 0x82C39558 | high | same position in Kill and HandleHealthDamage; uses AILinearTaskThreadManager::QInstance |
| `TaskQueueInterface::QueueActorKill` | 0x0087AFF0 | 0x82BD2F58 | high | same position in Kill/HandleHealthDamage, 3 args |
| `MiddleHighProcess::GetKnockState` | 0x008D8540 (proc vt+0x40C) | same slot | high | vtable + GetKnockedState condition switch matches KNOCK_STATE_ENUM |
| `MiddleHighProcess::SetKnockState` | 0x008D8560 (proc vt+0x410) | same slot | high | vtable |
| `MiddleHighProcess::KnockExplosion(Actor*, NiPoint3, float)` | 0x0091FEE0 (proc vt+0x418, **function created**) | 0x82CAF678 | high | vtable + Cmd_PushActorAway caller |
| `MiddleHighProcess::ClearKnockState` | 0x00920B80 (proc vt+0x420) | same slot | medium-high | vtable |
| `g_ReferenceProcessLists` (ProcessLists) | 0x011E0E80 | 0x8409A980 | high | JIP `ProcessManager::Get` + ECX at Kill's RemoveAliveActor call site |
| `Cmd_DamageActorValue` / `Cmd_KillActor` / `Cmd_DamageObject` (execute) | 0x005BDF20 / 0x005BE2A0 / 0x005D4B90 | — | high | CommandInfo @0x1194538 / 0x1191EC8 / 0x11941F0, execute at +0x18 |
| `Cmd_PushActorAway` (user name) | 0x005D6B60 | — | high | CommandInfo @0x11946A0 |
| `AdjustPushForce(ActorValueOwner*, int force)` (user name) | 0x00646580 (cdecl, returns float in ST0) | — | high | called by Cmd_PushActorAway with `actor+0xA4` and the int script arg; pushed args re-read 2026-10-04 (M10.4) |
| `CalcWeaponDamage(ActorValueOwner*, TESObjectWEAP*, float condition, float mult, int, bool, ContChangesEntry*, bool hasAmmo)` (no name) | 0x00644CE0 (cdecl, float in ST0) | — | medium-high | JIP `ContChangesEntry::CalculateWeaponDamage` pushes these 8 args; body: weapon null → base 1, skill AV 0x2D (Unarmed), + AV 0x38 (UnarmedDamage); GMSTs fDamageWeaponMult 0x11CE2C0, fDamageSkillBase 0x11CECE8 (+ skill mult 0x11CF350); condition → 0x00646D00. Used by M10.4b with weapon null (2026-10-04) |
| `g_fistsWeapon` (TESObjectWEAP*) | 0x011CA278 | — | high (JIP) | not used yet |
| maybe `ProcessLists::RemoveAliveActor` (not renamed) | 0x0096E2F0 | 0x82D120F8 | medium | ECX = g_ReferenceProcessLists, arg = actor, position in Kill |
| `MobileObject::GetCurrentAIProcess` (not renamed: 1921 refs, folded) | 0x008D8520 | 0x824E96A0 | high (body) | `return [this+0x68]` |

## Added in M3.1 (plugin render). Details in findings/render.md §6
| Name | PC address | Xbox address / symbol | Confidence | Source / how verified |
|---|---|---|---|---|
| `NiNode::NiNode(u16 capacity)` (alloc 0xAC with NiNew) | 0x00A5ECB0 | | high | Main init 0x0086D590 builds LandLOD/ObjectLODRoot/etc. this way |
| world `ShadowSceneNode*` (`GetShadowSceneNode(0)`) | `*(0x011F91C8)` | | high | 0x00450B80 reads `[0x11F91C8 + i*4]`; Main init attaches it under the world SceneGraph |
| DList `NewNode` (12 B node for the property list) | 0x0043A010 | | high | user name + JIP NiGeometry::AddProperty |
| `NiGeometryData` dtor body | 0x00A67C70 | | high | frees +0x20..+0x2C with `MemoryManager::Deallocate` (= FormHeap_Free 0x401030) |
| `NiGeometryData` +0x0E: dirty mask 0x0FFF, consistency 0x7000 (0 mutable, 0x4000 static) | — | `GetConsistency` 0x825343F0, `MarkAsChanged` 0x8392CDE8, `SetConsistency` 0x8392CD60 | high | Xbox symbols; PC base ctor 0x00A67B20 clears 0x7000 (mutable by default) |

### M3.2 additions (2026-10-02)
| Item | PC | Conf. | Source |
|---|---|---|---|
| `NiAlphaProperty::Create()` (cdecl, flags 0xEC) | 0x00A5CEB0 | high | disassembled (was medium/JIP) |
| `BSShaderNoLightingProperty` per-geometry setup (vtbl +0x9C) | 0x00B700D0 | high | Ghidra |
| `BSShaderNoLightingProperty` render-pass selector (vtbl +0xA0; srcTexture +0x60 → textured pass) | 0x00B6F5C0 | high | disassembly |
| BSShaderProperty flag test by index | 0x004A2020 | medium | call pattern (`push idx; call`) |
| `PlayerCharacter::Show1stPerson(bool)`: rewrites AppCulled of both player roots | 0x00951A10 | high | Ghidra, player.md §4 |
| `NiAVObject::SetAppCulled(bool)` thiscall, ret 4 | 0x00450F90 | high | Ghidra |
| TESWater reflection: `call SetAppCulled(0)` on the 3rd-person root (high-detail reflections) | 0x004EB75A | high | Ghidra + GOG bytes, player.md §4 |
| Scene sun light | `*(0x011F91C8)` +0xE0 LightingData* → +0xF8 NiLight* (ambient +0xC8, diffuse +0xD4, type +0x9D) | high (JIP) | render.md §8, M3.5 tint |

### M4.3b additions (2026-10-03)
| Item | PC | Conf. | Source |
|---|---|---|---|
| `PlayerCharacter::GetControlFlags()` (returns byte +0x680) | 0x005A03D0 | high | Ghidra; callers: Wait gate 0x0094239C, GetPlayerControlsDisabled |
| `PlayerCharacter::IsControlDisabled(u8 mask)` (thiscall, ret 4) | 0x005A03F0 | high | Ghidra |
| Wait key gate (`call 0x005A03D0`, any flag → no Wait, no message) | 0x0094239C | high | player.md §3 |
| HUD post-menu Movement check (`push 1; call 0x005A03F0` → state 0xC) | 0x0070C982 | high | player.md §3 |
| Activate handler Movement check (`push 1; call 0x005A03F0` → refuse + fail sound) | 0x00942F0C | high | player.md §3 |
| `HUDMainMenu::UpdateVisibilityState(state)` (cdecl) | 0x00771700 | high | Ghidra, 45 callers |
| `UpdateVisibilityState` internal Movement checks (`push 1; call 0x005A03F0`) | 0x00771836, 0x00771902 | high | player.md §3 |

### M9 audio side-task (2026-10-04), details in findings/audio.md
| Item | PC | Conf. | Source |
|---|---|---|---|
| `BSWin32Audio*` singleton pointer | 0x011F6D98 | high | JIP `BSWin32Audio::Get` + Ghidra xrefs |
| `BSWin32Audio::Init(HWND)`; `+0x38` = `IDirectSound8*` (written by DirectSoundCreate8), `+0x3C` primary buffer, `+0xA0` HWND, coop level DSSCL_PRIORITY | 0x00AEA230 | high | Ghidra |
| audio enabled flag (byte) | 0x011F6DA0 | medium | Ghidra (guards DirectSoundCreate8) |
| `BSAudioManager` instance; `volumes[0..5]` sliders at +0x140, `[6..11]` multipliers at +0x158 | 0x011F6EF0 | high | JIP + Ghidra 0x00ADAC30 |
| `BSAudioManager::GetEffectiveVolume(float vol, u32 soundFlags)` thiscall | 0x00ADAC30 | high | Ghidra (only caller SetVolume) |
| `BSWin32GameSound::SetVolume(float)` (vtable +0x3C) → `IDirectSoundBuffer::SetVolume` | 0x00AED660 | high | Ghidra, vtable entry 0x010A3C30 |
| `fMasterVolumeMult:Audio` (float, default 1.3) | 0x011F6E78 | high | Ghidra static init 0x00FB7650 |
| default slider values (stride 0xC) | 0x011F6E18 | medium | JIP SetGameVolume |
| NPC combat skips a Movement-disabled player (call site) | 0x008A04B2 in 0x008A03A0 | `IsControlDisabled(1)` on g_thePlayer; see player.md "Control flags vs. NPC combat" | high |
| AI procedures dropping a Fight-disabled player target | 0x008ECF20 / 0x00918120 | `IsControlDisabled(8)` | high |
| PlayerCharacter::IsImmobile | 0x009397E0 (vt+0x4B4) | `IsControlDisabled(1)` | high |
