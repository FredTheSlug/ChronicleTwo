# Debug mode

Retail cites are `ps2/src` file:line. PC cites are `port/src` or `tools/kazarov`.

## Config

`game.debug_mode` in `config.json`, default `true`. It is read once at boot, so a change takes effect on the next start.

- `true`: at boot, `MasterDebugCode` is set to `MASTER_DEBUG_CODE` and `MasterDebugModeOn` is set to 1. Retail `title.cpp:2377` then unlocks pad 2 without a developer save. Retail `title.cpp:2281` would otherwise read the save's debug code. Also enables the pad-1 chord below.
- `false`: nothing changes at boot. Retail behaviour: pad 2 unlocks only if the four shoulders are held on pad 2 at boot and a memory card save carries a debug code. The pad-1 chord is off.

This differs from retail on purpose: with the default, the developer-save gate is bypassed for every user. Set `"debug_mode": false` to restore it.

Boot code: `port/src/debug_mode.cpp` (`DebugModeBoot`), called from `port/src/mainloop.cpp` after the retail shoulder check.

## Pad-1 chord

Hold L1, L2, R1 and R2 on pad 1, then press R3. This toggles `DebugFlag`, the same switch as pad 2 Circle. It needs `debug_mode` true and `MasterDebugCode == MASTER_DEBUG_CODE`. It works while pad 2 is locked.

Keyboard (Kazarov `tools/kazarov/port/platform/input.cpp`, pad 1 bindings): L1 `Z`, L2 `Q`, R1 `F` or `X`, R2 `R`, R3 `B`. `Mouse3` also feeds R3, but it is also bound to `zoom_reset`.

`DebugFlag` starts off on every boot. Turn it on with the chord or with pad 2 Circle. The pad-1 controls below need it on.

## Retail pad-2 controls

These work with the pad-2 Circle toggle. Pad 2 is locked unless the boot unlock above succeeds.

- Pad 2 Circle (`ps2/src/mainloop.cpp:677-678`): toggles `DebugFlag`. Most of the controls below read `DebugFlag`.
- Pad 2 L1 (`mainloop.cpp:671`): toggles the performance meter.
- Pad 2 Select (`mainloop.cpp:724`): pauses. While paused, pad 1 Start or pad 2 Select resumes (`mainloop.cpp:732`). Pad 2 Start stores a frame image (`mainloop.cpp:737`). Pad 2 Right, or pad 1 Circle or Right, steps one frame (`mainloop.cpp:741`). The pause is not gated by `DebugFlag`.
- Town and georama, pad 2 Right/Left: scrubs the time by 0.1 (`ps2/src/editloop.cpp:1080-1092`). Pad 2 Up/Down set the time and toggle the time step.
- Town, pad 2 R2 (`editloop.cpp:1879`): toggles rain.

## What `DebugFlag` unlocks on pad 1

- Title: Start+Select opens the debug menu (`ps2/src/title.cpp:848`). The menu's sub-views (maps, dungeons, title, chrview, texview, mapview, sound, movie, language, items, save edit, `dbg/game%d.cfg`, save convert) and its event select were not re-checked line by line for this doc.
- Future map select: `ps2/src/mainloop3.cpp:115` (`FutureMapSelect`).
- Dungeon: R3 opens the dungeon debug menu (`ps2/src/dng_main.cpp:2177`, `dngDebugStart`, reached only under `DebugFlag` at `dng_main.cpp:2157`). R3 again closes it (`ps2/src/dng_debug.cpp:311`). Start+Select exits the floor (`ps2/src/dng_main.cpp:1133`).
- Town and georama: R3 opens the edit debug menu (`ps2/src/editloop.cpp:1719-1730`). With `DebugFlag`, R2 makes the material check pass (`ps2/src/editmenu.cpp:3955`, `5511`; `editmode.cpp:2055`). L3, or Square outside place mode, moves 3x (`ps2/src/editmode.cpp:1454-1456`).
- Menus: R3 toggles `menu_debug_flag` (`ps2/src/menumain.cpp:1432`). The flag gates the extra menu paths in `menuop.cpp:1944`, `3183` and `menumap.cpp:1571`.
- Events: Start skips a movie (`ps2/src/event_func.cpp:4776`).

## PC gaps

- The keyboard feeds pad 1 only (`tools/kazarov/port/platform/input.cpp`, `Compose`). Pad 2 needs a second controller.
- `developer_menu` (paddle 2) and `debug_menu` (paddle 1) are bound in `input.cpp` but nothing reads them. See `docs/port/HANDOFF.md` 86 and 91.
- Capture and Play on the debug top screen hit the `CGamePad` capture stubs (`port/src/gamepad.cpp`, the first definitions).
- GyoRace hits the `sgLoopGyoRace` stub (`port/src/gyorace.cpp`).
