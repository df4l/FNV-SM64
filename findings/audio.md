# audio.md: how FNV plays sounds (M9 side-task, 2026-10-04)

PC = FalloutNV.exe 1.4.0.525 (Ghidra, Steam unpacked). Layouts cross-checked against JIP LN `nvse/GameSound.h`.

## 1. Backend: DirectSound 8
- `BSWin32Audio` singleton: `*(BSWin32Audio**)0x011F6D98` (JIP `BSWin32Audio::Get`, 80+ xrefs in Ghidra).
- Init `BSWin32Audio::Init(HWND)` **0x00AEA230**: `DirectSoundCreate8(NULL, this+0x38, NULL)`, `SetCooperativeLevel(this+0xA0 hwnd, 2 = DSSCL_PRIORITY)`, `GetCaps(this+0x40)`, primary buffer (`dwFlags 0x91` = PRIMARYBUFFER|CTRL3D|CTRLVOLUME) stored at **+0x3C**, 3D listener from it. **+0x38 holds the `IDirectSound8*` itself** (JIP types it `IDirectSound8**`; Init writes the interface pointer into the field). Guard byte `0x011F6DA0` = audio enabled; `+0x05` = init done. high.
- Every game sound is a `BSWin32GameSound` (0x230) owning `IDirectSoundBuffer8*` at **+0x19C** (and a second buffer at +0x1A0 for the 3D/reverb path). Music/radio streams are Ogg (`ov_*` imports) decoded by the game; Bink videos use `BinkOpenDirectSound`.
- So "the way the game plays sounds" at the device level = secondary DirectSound buffers on one `IDirectSound8`. A buffer we create on that same device is mixed by the game's DirectSound device with all game sounds, follows the game's output device, and (no `DSBCAPS_GLOBALFOCUS`, as the game's own buffers) goes silent when the game window loses focus.

## 2. Volume path (what the in-game sliders do)
`BSWin32GameSound::SetVolume(float vol)` **0x00AED660** (vtable slot +0x3C, vtable entry at 0x010A3C30), high:
1. `vol` clamped to [0,1], stored at +0x24.
2. `eff = BSAudioManager::GetEffectiveVolume(vol, soundFlags)` **0x00ADAC30** (thiscall on `BSAudioManager` 0x011F6EF0, returned by 0x00AD9060):
   `eff = vol × volumes[0] × volumes[6] × fMasterVolumeMult × category`, where category by `soundFlags` (+0x08):
   | flag | category | slider × mult |
   |---|---|---|
   | 0x800000 or 0x800 (music) | Music | `volumes[4] × volumes[10]` (+0x150, +0x168) |
   | 0x8 (footsteps) | Foot | `volumes[1] × volumes[7]` (+0x144, +0x15C) |
   | 0x4 (voice) | Voice | `volumes[2] × volumes[8]` (+0x148, +0x160) |
   | 0x100000 (radio) | Radio | `volumes[5] × volumes[11]` (+0x154, +0x16C) |
   | none of the above | **Effects** | `volumes[3] × volumes[9]` (+0x14C, +0x164) |
   `volumes[0..5]` (+0x140..+0x154) = the Master/Foot/Voice/Effects/Music/Radio sliders (JIP `volumes[]`, Get/SetGameVolume); `volumes[6..11]` (+0x158..+0x16C) = per-category multipliers (fades/ducking). `fMasterVolumeMult:Audio` = float at **0x011F6E78** (INI setting, default 1.3; static init 0x00FB7650).
3. A muted-when-submerged rule forces 1e-5 (state 0x10000 without 0x200, no flag 0x1000).
4. `eff` clamped to ≥ 1e-5, then `mB = (int)(2000 × log10(eff))` (constant 2000.0 at 0x01073490; the CRT call 0x00ECBC80 is FID-ambiguous, log10 is the standard DirectSound conversion: medium, the user's ear test settles it), minus static attenuation words +0x18/+0x1C/+0x20 (+0x1E for some 3D sounds), clamped to [−10000, −1], then `IDirectSoundBuffer::SetVolume(mB)` (vtable +0x3C). State 0x400 (muted) → −10000.

Default slider values (restore table used by JIP SetGameVolume): `0x011F6E18 + i×0xC`.

## 3. Use for Mario (M9 design, replaces D4)
- Create one streaming secondary buffer (32 kHz, 16-bit stereo, `DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2`, 2D) on `BSWin32Audio+0x38`.
- Each game frame, recompute the volume with the formula above as an **Effects** sound (vol = `[Audio] fVolume` from sm64.ini, default 1.0) and call `SetVolume` only when it changes. The in-game Master/Effects sliders then act on Mario exactly as on a game effect.
- libsm64 outputs one mixed stream (SA never starts SM64 music), so one category is enough.
- No engine patch needed: only reads of `0x011F6D98`, `0x011F6EF0+0x140..0x16C`, `0x011F6E78`, and COM calls on the game's device.
