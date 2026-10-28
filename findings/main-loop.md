# P1: Main loop, timing, pause/loading state

All PC addresses are FalloutNV.exe 1.4.0.525 (Ghidra). Xbox addresses are Fallout_Debug.exe (IDA, symbols).
"(user)" = name already set by the user in Ghidra (kept as is, identification added as a comment only).

## 1. Frame structure

```
WinMain-like loop            PC 0x0086A850 "LaMainLOOP" (user)
 ├─ PeekMessage pump
 ├─ [NVSE hook 0x0086B386]  -> kMessage_MainGameLoop (before OnIdle; also fires while the device is lost)
 ├─ device->TestCooperativeLevel (vtbl+0xC)
 │    <0 : NiDX9Renderer::BeginOffScreenFrame (0x86BA10) / EndOffScreenFrame (0x86BA90), Sleep(50)
 │    ok : Main::OnIdle(g_TESMain)            PC 0x0086E650 "FonctionDinteret2" (user) == Xbox ?OnIdle@Main@@ 0x82BBE778
 └─ window inactive: audio pause, Sleep(50)
```

`Main::OnIdle`: PC call order, verified 1:1 against Xbox (≈60 calls, same guards):

| # | PC | Xbox symbol | Notes |
|---|---|---|---|
| 1 | FUN_0086a830(fUpdateTexturesOnIdleTime) | Main::UpdateTextures | |
| – | GetAsyncKeyState(Tab)+(Alt) → return | — | **PC only**: whole frame skipped while Alt+Tab is held |
| 2 | g_bIsMenuMode(0x11DEA2B) = isMenuMode() \|\| Interface::IsPipboyOpening (0x709BC0) | Main::bInMenuMode | recomputed twice per frame |
| 3 | **Main::OnIdle_UpdatePlayer 0x86F940** | 0x82BC10A8 | PlayerCharacter::Update(dt × VATS::GetPlayerUpdateMult) |
| 4 | Main::OnIdle_UpdateImageSpace 0x86FD90 | 0x82BC17D0 | |
| 5 | FUN_004e1610 | SleepManager::RunSleep | low confidence |
| 6 | ++DAT_011a2fe0 | Main::iFrameCounter | |
| 7 | **Main::OnIdle_UpdateTimer 0x86F260** | 0x82BC05F8 | dt computed here |
| 8 | **Main::OnIdle_PollControls 0x86F390** | 0x82BC07D8 | input polled here |
| 9 | FUN_00c3dbf0 | IOManager::UpdateQueue | |
| 10 | isInStartMenu (user) / isInLoadingScreen (user) → TES::ShowLoadingMenu (FUN_00457d70) | StartMenu::MainMenuIsActive / Interface::IsInGameLoadingMenuOpen | |
| 11 | FUN_0086efe0 | Main::OnIdle_ScaleLOD | |
| 12 | FUN_00b6dd00(texMgr, WorldRootCamera, menu) | BSTextureManager::Update | |
| 13 | FUN_0086f450, FUN_007011d0(1) | OnIdle_HandleMenuBackground, FaderManager::UpdateFaders | |
| 14 | spWorldRoot->vtbl[0x104](sec), FUN_00867a40(sec), FUN_00455640 | SetViewDistanceBasedOnFrameRate, Calendar::Update, TES::RunAnimations | !menu && !freeze |
| 15 | FUN_00978550 | ProcessLists::RunActorScripts | |
| 16 | FUN_009777a0, FUN_008d0600, FUN_0096eb40, FUN_0096e9b0 | ProcessLists::UpdateRadiationList / UpdateHighListCollisionBox / ChangeProcessLevelTempList / UpdateFollowerTempList | |
| 17 | FUN_00868850 / FUN_00868d10 | GarbageCollector::Update / ClearTempEffects | |
| 18 | FUN_006652e0(cam, menu\|\|freeze) | BSTreeManager::Update | |
| 19 | **Main::OnIdle_UpdateCurrentGridCell 0x86FBE0** | 0x82BC1478 | cell attach/detach |
| 20 | FUN_00710ab0 → SceneGraph::SetCameraFOV (user) | PlayerCharacter::GetWorldFOV → BSSceneGraph::SetCameraFOV | skipped in VATS playback |
| 21 | FUN_0045bc80 / FUN_00b5ac90 / FUN_0045b070 | TES::ResetAllMultiBoundNodes / ShadowSceneNode::UpdateOcclusionPlaneVisibility / TES::UpdateMultiBoundVisibility | |
| 22 | FUN_008ca070 or FUN_008c80e0+FUN_008c78c0 | AI tasks / AILinearTaskThreadManager::StartThreads | |
| 23 | **Main::OnIdle_UpdateAnimationsAndEffects 0x86FC60** | 0x82BC1588 | |
| 24 | Main::OnIdle_DoInterfaceIdle 0x86FD70 | 0x82BC1758 | |
| 25 | FUN_006ebc50, FUN_00552ba0, FUN_00991500 | PathManager::Update, NavMeshRender::Update, CombatManager::Update | |
| 26 | FUN_0057ab70, FUN_00b60040 | CheckWithinMultiBoundTask::DoAttachments, ShadowSceneNode::ProcessAllQueuedLights | |
| 27 | **"FonctionDinteret" 0x86FF70 (user)** | Main::Swap 0x82BC19B8 | render + present (medium) |
| 28 | Main::PostSwapProcess 0x8705D0 | 0x82BC21C0 | |
| 29 | DAT_011c6fbb / FUN_004dc360 | Renderer::bDoResize / DoResize | |
| 30 | FUN_0086f6a0, FUN_00870610 | UpdateNonRenderSafeAITasks, OnIdle_PostThreadsProcess | |

**Consequences for SM64 (findings, not implementation):**
- In frame N, the player is updated (3) **before** dt (7) and input (8) are refreshed. The player therefore uses frame N−1's timer and controls.
- Input becomes valid after (8). Scene transforms are final after (23). Rendering happens in (27).
- The NVSE `MainGameLoop` message fires **before** `Main::OnIdle`, i.e. with the previous frame's dt and input. Candidate tick/render points are listed in `open-questions.md`.

## 2. Timing

| Item | PC | Xbox | Layout / notes | Conf. |
|---|---|---|---|---|
| `appTimer` (BSTimer instance) | **0x011F6394** | `appTimer` 0x840B796C | = NVSE `TimeGlobal`/JIP `TimeGlobal::Get()`; `+0x0C float secondsPassed` (frame dt, s), `+0x14 msPassed` | high |
| `BSTimer::GetSeconds` | 0x0084D030 | 0x8252D260 | returns `this+0x0C` | high |
| `BSTimer::Update` | 0x00771520 | 0x82A3DA08 | | high |
| `Main::fFrameAnimTime` | 0x011DEA30 (float) | 0x84098208 | menu ? 0 : dt × fAnimationMult | high |
| `fAnimationMult` (INI setting) | 0x011C5724 | | Setting::Float = 0x00403E20 | high |
| `bhkWorld::fTauRatio` | 0x011AFE60 | 0x8404CD50 | ← GMST fHavokTauRatio 0x011D1320 | high |
| `bhkWorld::SetDeltaTime(float, bool, bool)` | 0x00C66760 | 0x832A59C8 | Havok step dt | high |
| `bChangeTimeMultSlowly` setting | 0x011DEDBC | | Setting::Bool = 0x00408D60 | medium |
| `pVATS` (VATS singleton) | 0x011F2250 | 0x840ABDB0 | mode @ +0x08, 4 = VATS_PLAYBACK. NVSE calls it `VATSCameraData` | high |
| `VATS::GetPlayerUpdateMult` | 0x009C8CC0 | 0x82D9D940 | player dt multiplier (slow-mo) | high |
| real-time seconds | FUN_007013e0 (`this+0xC / DAT_011ac3a0`) | BSTimer::GetRealTimeSeconds | used by PollControls (unscaled time) | medium |

The SM64 30 Hz accumulator should use `appTimer.secondsPassed` (scaled game time, 0 in menus via `fFrameAnimTime`) or real time. Which one is a design question (D6).

## 3. Pause / menu / loading state

| State | PC | Xbox | Conf. |
|---|---|---|---|
| in menu mode (incl. Pip-Boy opening) | `g_bIsMenuMode` 0x011DEA2B (user); `isMenuMode()` 0x00702360 (user) | Main::bInMenuMode, Interface::IsInMenuMode | high |
| fader to black visible (fades, fast travel) | `g_bIsInPauseFade` (user, = FUN_00701450(1)) | Main::bFaderToBlackVisible = FaderManager::IsFaderVisible(FADER_TO_BLACK) | high, **user name differs from Xbox meaning** |
| in dialogue | `g_DialogMenu2` (user, = FUN_007050d0) | Main::bInDialog = Interface::InDialog | high |
| console open | `g_bConsoleOpen` (user, = FUN_00703d50) | Main::bInConsole | high |
| loading screen | `isInLoadingScreen` 0x00705EA0 (user) = `*(0x011DA0C0) != 0` (LoadingMenu*) | Interface::IsInGameLoadingMenuOpen | high |
| main menu | `isInStartMenu` 0x0070EDF0 (user), StartMenu* 0x011DAAC0 | StartMenu::MainMenuIsActive | high |
| freeze time (`tfc`/`freezetime`) | `Main+0x07 bFreezeTime` | Main::bFreezeTime | high |
| fly camera (`tfc`) | `Main+0x06 bFlyCamera` | Main::bFlyCamera | high |
| simulation running | `(!bInMenuMode \|\| bFaderToBlackVisible) && !bInConsole && !bFreezeTime` | same expression (Main::OnIdle) | high |

## 4. Scene graph / camera anchors (also P4)

| Item | PC | Xbox | Conf. |
|---|---|---|---|
| `Main::spWorldRoot` (NiPointer<SceneGraph>) | **0x011DEB7C** (`g_SceneGraph`, user) | 0x84098358 | high |
| `Main::WorldRootCamera()` | 0x00524C90 (`GetWorldSceneGraph`, user: misnamed, returns the NiCamera) | 0x8268AAA0 | high |
| `BSSceneGraph::GetCamera` | 0x006629F0 (`GetCamera___`, user) | 0x8253C018 | high |
| `g_TESMain` (Main*) | (user symbol); NVSE/JIP `OSGlobals**` 0x011DEA0C | `Main` | high |

## 5. D3D device lost/reset (PC only)

- Main loop: `TestCooperativeLevel < 0` → Begin/EndOffScreenFrame + Sleep (no reset there).
- Reset notification (probable): `FUN_00872570(bool)` registered by `FUN_0086bae0(fn, 0)` at renderer init and removed by `FUN_0086bb20`. A lost-device pair (`RetourneLeChiffreUN` via `FUN_0086bbd0`/`FUN_0086bc10`) is also probable. This matches the Gamebryo `NiDX9Renderer::AddResetNotificationFunc` / `AddLostDeviceNotificationFunc` API. Confidence: low-medium (one signal, no Xbox counterpart).
