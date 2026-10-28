# P7: Scale and coordinate frames (DONE, 2026-10-01)

PC = FalloutNV.exe 1.4.0.525; Xbox = Fallout_Debug.exe.

## 1. Units (verified in the binary)
| Item | PC | Value | Conf. | How verified |
|---|---|---|---|---|
| Game units per metre (`k_dUnitsPerMeter_69_99`, double) | 0x010120E8 | **69.99125671386719** | high | constant read from .rdata |
| `fHk2BSScale` (Havok → game units) | 0x011C5880 (runtime) | 69.99125 / 10.0 = **6.99913** | high | static init 0x00F3D3B0: `[0x10120E8] / [0x101E170]=10.0` |
| `fBS2HkScale` (game → Havok) | 0x011C582C (runtime) | 1 / 6.99913 = **0.142875** | high | static init 0x00F3D3D0: `1 / fHk2BSScale`; Xbox `NI2HK` uses fBS2HkScaleSC (P2) |
So **1 Havok unit = 0.1 m = 6.99913 game units**, and **1 m = 69.99 game units** (1 unit ≈ 1.4288 cm). Havok mesh vertices (P5) and the character proxy velocity (P2) must be multiplied by 6.99913 to get game units.

## 2. Axes and angles
- **World: X = east, Y = north, Z = up** (right-handed). Z-up is confirmed by `NiCamera::LookAtWorldPoint(…, NiPoint3::UNIT_Z)` in UpdateCamera (P4); X/Y orientation by the heading convention below.
- **Heading (rotZ, radians)**: `GetZAngleFromVector` (PC 0x004B13C0 == Xbox 0x825E05E0, identical body) = atan2(x, y) clamped to [0, 2π) by `ClampAngle` (PC 0x004B1480). **0 = +Y (north), π/2 = +X (east), clockwise seen from above.** Forward vector = (sin h, cos h, 0). Conf. high.
- REFR rotation fields `rotX/rotY/rotZ` @ +0x24 (NVSE), radians.
- Camera forward = column 0 of the world rotate matrix (P4); pitch is `MobileObject::GetLooking`.
- NiAVObject transforms are row-major NiMatrix3 + translate + scale (PC offsets in P2/P4).

## 3. FNV ⇄ SM64 conversion (documentation, derived from the above + the SA mod)
SM64 is Y-up; GTA SA (also X east / Y north / Z up) uses `sm64 = (x, z, −y) / MARIO_SCALE` with MARIO_SCALE = 0.0085 m per SM64 unit. For FNV:
- `FNV_PER_SM64 = 0.0085 × 69.99126 ≈ 0.5949` game units per SM64 unit (same visual size as in the SA mod; tunable).
- position: `sm64 = (fx, fz, −fy) / 0.5949`; back: `fnv = (sx, −sz, sy) × 0.5949` (normals: same swizzle, no scale).
- yaw: FNV heading h (clockwise from +Y) ↔ SM64 faceAngle a (0 faces +Z, which is FNV −Y): **a = π − h** (inferred; verify sign in game).
- SM64 collision is int32 SM64 units: FNV exterior coordinates (±~200 000 game units) / 0.5949 ≈ ±336 000, well inside int32, and libsm64 upstream has no ±8192 level bound (requirements §0).
- Havok vertices → SM64: `v_hk × 6.99913 / 0.5949 ≈ v_hk × 11.765`.

## 4. Notes
- Ghidra's FID analyzer mislabels the `atanf` call in GetZAngleFromVector as `_logf` (library false positive, like `OnStatusTextChange`).
