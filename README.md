# FNV-SM64

An [xNVSE](https://github.com/xNVSE/NVSE) plugin that replaces the player of **Fallout: New Vegas** (PC, 1.4.0.525) with **Mario from Super Mario 64**, powered by [libsm64](https://github.com/libsm64/libsm64). It is modelled on [sm64-san-andreas](https://github.com/headshot2017/sm64-san-andreas) (GTA SA), and uses that project's libsm64 fork.

> **Status: work in progress, playable but rough.** Everything below was tested by hand in game, one step at a time. Expect bugs.

## What you need

- Fallout: New Vegas 1.4.0.525 (GOG and Steam both work) with xNVSE 6.4.x.
- A **legally obtained Super Mario 64 US ROM** (`sm64.us.z64`). It is never shipped with this repo: Mario's geometry, textures and sounds are read from the ROM at runtime.
- A 32-bit `libsm64.dll` built from the fork (see below).

Runtime files go in `Data\NVSE\Plugins\sm64\`: `libsm64.dll`, `sm64.ini` and the ROM. The plugin DLL itself is `Data\NVSE\Plugins\nvse_sm64.dll`.

## Building

```
git clone --recurse-submodules https://github.com/df4l/FNV-SM64.git
```

- **Plugin:** open `nvse_sm64.sln` (Visual Studio 2026, toolset v145, Release|Win32). The project expects the xNVSE sources as a sibling folder (`..\nvse\nvse`, as in `nvse_plugin_example`), so clone this repo next to the NVSE tree. The post-build step copies the DLL to `%FalloutNVPath%\Data\NVSE\Plugins`.
- **libsm64:** `libsm64\build.cmd` builds the fork from `third_party\sm64-san-andreas\libsm64` as a 32-bit DLL with MSVC + Ninja (a small patch, `libsm64-msvc.patch`, removes the GNU-only bits). The generated `src/decomp/mario/*` files come from the fork's `import-mario-geo.py` and need Python.

## Controls and debugging

- **F8**: master toggle. Despawns Mario and gives the vanilla player back (and back again).
- **F10**: dumps the state (positions, Mario's action, collision counts, timings) to the console and the log.
- Gameplay uses the **game's own key bindings** (and the left stick of a gamepad): move, jump, punch (Attack), crouch (Sneak), walk (hold Run).
- Every subsystem has a switch in `sm64.ini` (`[Subsystems]`, `[Collision]`, ...) so a faulty part can be turned off without rebuilding. The plugin logs every init stage and important decision to `nvse_sm64.log` in the game folder.

## What is done

| Milestone | What it does | State |
|---|---|---|
| M0 Toolchain | Plugin + fork build, libsm64 loaded, ROM checked, `sm64_global_init` | done |
| M1 Frame tick | Fixed 30 Hz Mario tick with an accumulator, gated on pause / menus / loading / console, toggle key | done |
| M2 Spawn + follow | Mario spawns on the player, the player ref follows him, vanilla movement and physics disabled | done |
| M3 Render | Mario drawn as dynamic `NiTriShape`s (coloured + textured parts), lit by the game's sun, smooth interpolation at any FPS, player body hidden | done |
| M4 Input | Keyboard, mouse and gamepad through the game bindings, camera-relative; weapon / VATS / hotkey / grab controls blocked while Mario is active | done |
| M5 Static collision | Terrain, buildings, statics and interior pieces from Havok shapes streamed into SM64 surfaces around Mario, outside and inside | done (known bugs deferred) |
| M6 Camera | Forced 3rd person, raised zoom limits, proxy-follow camera | done |
| M7 Lifecycle | Save / load / quicksave / new game / menu / fast travel / `coc` / doors / Alt-Tab without stale geometry or crashes | done |
| M8 Dynamic surfaces | Doors, keyframed bodies and larger loose objects (crates, barrels, furniture) as moving SM64 objects | done (lift riding untested) |
| M9 Audio | Mario's voice and effects played through the game's own DirectSound device, following the Master and Effects volume sliders | done |
| M10 Gameplay | Health sync both ways, water level, NPCs as colliders (sized from their bounds), Mario's punches / kicks / ground pound / slides hurt NPCs using the vanilla unarmed damage formula (tunable) | M10.1 to M10.4 done |

The reverse engineering behind all this (addresses with confidence levels, struct layouts, hook sites, scale and frame conventions) is in [`findings/`](findings); `findings/addresses.md` is the master table.

## Planned

- **M10.5**: wall attacks on destructibles.
- **M10.6**: Bowser-style grab and throw of NPCs.
- **M11 Polish**: edge states (dialogue, terminals, Pip-Boy, VATS, sitting, death), performance, a toggle console command, config clean-up.
- Open items from earlier milestones: lift riding test, capsule and sphere collision shapes, the interior collision bugs deferred at the end of M5, the SM64 music (`sm64_play_music` is not used), Voice / Music slider behaviour and Alt-Tab silence checks.
- Maybe: an SM64-style camera, instead of the proxy-follow one.

## Licence and legal

No Nintendo data is included: the ROM is supplied by the user and read at runtime. libsm64 and the SM64 decompilation it is built on have their own licences, which apply to any binary built from them; check them before redistributing a build. This project is not affiliated with Nintendo, Bethesda or Obsidian.
