# P3: Input (DONE, 2026-10-01)

PC = FalloutNV.exe 1.4.0.525. Xbox = Fallout_Debug.exe. The Xbox input class `Controls` (base `XInputManager`) is console-specific: its *roles* transfer, its *layout* does not. The PC class is `OSInputGlobals` (singleton `*(OSInputGlobals**)0x011F35CC`).

## 1. Where input is refreshed (frame timing, see main-loop.md)
`Main::OnIdle_PollControls` (0x86F390) → **`OSInputGlobals::Poll` 0x00A23010** (+ FUN_00A257C0: rumble update when the gamepad is active and `+0x1B88` vibration is enabled). Input is valid after this call and stale before it (e.g. in NVSE MainGameLoop).

## 2. Raw device state (best source for Mario)
| Item | PC | Notes | Conf. |
|---|---|---|---|
| `g_xinputState` XINPUT_STATE (pad 0) | **0x011F35A8** | +0 dwPacketNumber, +4 wButtons, +6 bLeftTrigger, +7 bRightTrigger, **+8 sThumbLX, +0xA sThumbLY**, +0xC sThumbRX, +0xE sThumbRY (standard XInput layout; reads of LX/LY/RX/RY at 0x11F35B0/B2/B4/B6 confirm) | high |
| `g_xinputStatePrev` | 0x011F35B8 | copy of previous frame | high |
| `g_bGamepadActive` | 0x011F35C8 | XInputGetState(0) == ERROR_SUCCESS && !controllerDisabled (OSInputGlobals+0x0) | high |
| Keyboard DIK states | OSInputGlobals+0x18F8 (prev +0x19F8), 256 bytes | DirectInput GetDeviceState | high (Poll body + JIP) |
| Mouse DIMOUSESTATE | +0x1B24 (lX,lY,lZ, buttons[4]; prev +0x1B38) | | high |
| Imports | XInputGetState / XInputSetState (XINPUT1_3), DirectInput8Create (DINPUT8) | thunks 0x9F996E / 0xEE820A / 0xEE7F10 | high |

## 3. Game-level actions (bindings applied)
| Item | PC | Xbox analogue | Conf. |
|---|---|---|---|
| `OSInputGlobals::GetControlState(ControlCode, KeyState)` (user name `GetControlState`) | **0x00A24660** | Controls::GetUserAction 0x82F981E0 | high (NVSE detours) |
| `OSInputGlobals::GetButtonState(device, key, state)` | 0x00A24560 | dispatch: 0 keyboard (0xA24180), 1 mouse (0xA23A50), 3 XInput (0xA23E10), 2+ DI joystick (0xA234C0 buttons / 0xA235F0 POV) | medium (PC-only, body) |
| `OSInputGlobals::GetJoystickAxis(device, axis)` | **0x00A23390** | Controls::GetJoystickAxis 0x82CDBFE8 | high |
| `OSInputGlobals::maybe_ForceGamepadButton(btn)` | 0x00A23D40 | (XInputManager::SetButtonState-like) | low-medium: writes full deflection / 0xFF triggers / button bits into g_xinputState |

ControlCode (JIP enum, matches the 0x1C bind tables): 0 Forward, 1 Backward, 2 Left, 3 Right, **4 Attack**, 5 Activate, 6 Aim/Block, 7 ReadyItem, **8 Crouch(Sneak)**, 9 Run, 0xA AlwaysRun, 0xB AutoMove, **0xC Jump**, 0xD TogglePOV, 0xE MenuMode, 0xF SleepWait, 0x10 VATS, 0x11–0x18 hotkeys, 0x19 QuickSave, 0x1A QuickLoad, 0x1B Grab. Special codes > 0x1B: Escape, Console, Screenshot.
KeyState (JIP/NVSE): 0 Held, 1 Pressed, 2 Depressed, 3 Changed (medium: from headers).
Bind tables: OSInputGlobals+0x1B94 keyboard, +0x1BB0 mouse, +0x1BCC joystick, +0x1BE8 controller (0xFF = unbound).

## 4. How the player turns the stick into movement
In **`PlayerCharacter::Update` 0x0093E860** (function created this session; Xbox ?Update@PlayerCharacter@@ 0x82CD5BF8), around 0x941100:
- if `FUN_004B71D0()` (INI flag 0x11D8A84 && interface check, probably "gamepad enabled"): read axis 7 (LX), 9 (RX), 8 (LY);
- deadzone: |LX|,|LY| < **7849 (0x1EA9)** → 0, |RX| < **8689 (0x21F1)** → 0 (the XInput SDK constants);
- scale: v × 100 / 0x7FFF → **[-100, 100]**.
Keyboard movement comes from GetControlState(Forward..Right) (digital).
SM64 mapping idea, kept in open-questions: stickX = LX/32767 and stickY = −LY/32767 after the same deadzone; keyboard gives ±1.

## 5. Suppressing game reactions to Mario's inputs
- Engine mask: `PlayerCharacter::SetControlsDisabled` (P2): Movement/Look/PipBoy/Fight/POV/Rollover/Sneak, **no Jump bit**.
- NVSE (already shipped): call-site detours on GetControlState with `g_disabledControls` flags kFlag_EnterVATS, kFlag_Jumping, kFlag_AimingOrBlocking, kFlag_Running (and firing hooks). The plugin can reuse these instead of new hooks.
- NVSE `DIHookControl` (keyboard/mouse only) can disable individual keys.
- Gamepad: no existing per-button disable. Candidates are the GetControlState call sites or `maybe_ForceGamepadButton`'s inverse (clear bits in g_xinputState after Poll). TODO only if needed.

## 6. Open
- Exact semantics of `FUN_004B71D0` / 0x11D8A84 (bUseGamepad INI?).
- The mouse look path (JIP hooks 0xA239E0 GetMouseMovement) → P4 camera.

## 7. ControlCode values confirmed (2026-10-03, M4.3)
Ghidra enum `ControlCode` (types from the Stewie Tweaks PDB): 0 Forward … 6 Aim, 7 ReadyItem, 8 Crouch, 9 Run, 0xA AlwaysRun, 0xB AutoMove, 0xC Jump, 0xD TogglePOV, 0xE MenuMode, 0xF Rest, 0x10 VATS, 0x11 Hotkey1, **0x12 AmmoSwap** (not Hotkey2), 0x13–0x18 Hotkey3–8, 0x19 QuickSave, 0x1A QuickLoad, 0x1B Grab, 0x1C Escape, 0x1D Console, 0x1E Screenshot. high.
`GetControlState` 0x00A24660 prologue: `55 8B EC 83 EC 14` (push ebp / mov ebp,esp / sub esp,0x14), then `mov [ebp-0x10],ecx` at 0xA24666. GOG file bytes are identical; JIP and xNVSE don't hook the entry (they detour call sites). An entry JMP + NOP with this prologue relocated is safe. high.
Gamepad mode: only Escape among the special codes (≥ 0x1C) is handled, so **the console can't be opened while a pad is connected** (vanilla).
