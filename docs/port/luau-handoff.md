# Luau migration handoff (M1 and M2 paused)

Status at pause: M0 is complete. M1 (transpiler) and M2 (runtime) were only researched; no code for either exists yet. The working tree builds as before, because this phase changed no code.

## What exists

- `docs/PORT_LUAU.md`: script kinds, tools and catalog, plus a draft **Runtime contract** section. That section is the agreement between the transpiler and the runtime.
- `tools/luau/stb/` (M0): decoder (`program.py`), CFG (`cfg.py`), abstract stack analysis (`analysis.py`), handler source parsing (`sources.py`), catalog (`catalog.py`), scan, report and fixture assembler.
- `tools/luau/survey.py`, `commands.json` (1,081 commands), `REPORT.md`, `curation.json` (empty).
- `tools/luau/test_stb.py`: 20 tests. They run through CTest as `Luau.Tools` and `Luau.Survey` (`port/src/tests/cmake/luau.cmake`).
- The plan: `luau_plan.md` in the session scratchpad. The user's decisions below override it.

Nothing exists yet under `port/src/luau/`, `port/cmake/luau.cmake` or `tools/luau/stb2luau/`, and there is no `dc2data export-scripts`.

## Verified

- `python -m unittest discover -s tools/luau -p "test_*.py"`: 20 tests, OK (2.3 s).
- `.\build.bat windows-x64` (incremental, `port/build/windows-x64`): exit 0 in 107 s. It recompiled the Kazarov Rust crate and relinked `dc2data.exe` and `darkchronicle.exe`. The PS2 build was not run, since nothing under `ps2/` changed.

## Research findings (needed to resume)

### VM (`ps2/src/runscript.cpp`)

- Opcodes run 1 to 30. Opcode 22 is a no-op label. LOAD and LOAD_ADDR take the address mode in `arg2`: LOCAL 0x1, LOCAL_INDEX 0x2, POINTER_INDEX 0x4, LOCAL_FLOAT 0x8, LOCAL_INDEX_FLOAT 0x10, POINTER_INDEX_FLOAT 0x20, GLOBAL 0x40, GLOBAL_FLOAT 0x200.
- A `*_FLOAT` LOAD retags the stored slot in place: the int bits are reinterpreted as a float. LOAD_ADDR does not retag.
- Binary operators pop rhs, then lhs. INT op INT wraps at 32 bits. If either operand is FLOAT, the result is FLOAT. STR and PTR operands exit.
- DIV checks the divisor's raw bits for zero before popping lhs, so a +0.0f divisor exits and -0.0f does not.
- MOD, AND, OR and NOT accept INT operands only.
- SIN and COS take radians and use `sinf`/`cosf`.
- FTOI truncates.
- `is_true` is false only for INT 0.
- `STORE` pushes its value back.
- JMP_TRUE and JMP_FALSE pop the condition. When the jump is taken and `arg2` is set, they push 1 or 0. In skip mode they do nothing and do not pop.
- EXT: `sp -= arg1`, then it calls `ext(sp, arg1)` unless skipping. The first slot is the command. Nothing is pushed; handlers write outputs through PTR slots.
- CALL: `frame = sp - arg`. Locals above the arguments are zeroed. RET resets `sp` to the frame and pushes the return value. A top-level RET sets `result` and `end`.
- WAIT suspends with pc on the next instruction. In skip mode it falls through.
- SKIP_END increments `skip_end_count`. If skipping, it clears the flag and suspends.
- PRINT formats INT with `%d`, STR with `%s` and FLOAT with `%f`, prints nothing for PTR, and adds no separator or newline.
- `run(no)` returns -1 when the program is missing, 0 when the script ended and 1 when it suspended. `no < 0` runs main.
- `load()` takes the SB2 globals from the start of the value array and zeroes them.
- `CRunScript` is 0x54 bytes on the PS2 (layout in `ps2/include/runscript.hpp`).

### Port overrides

- `port/src/runscript.cpp` overrides only `check_program` (null-safe) and `ext` (range check, then `script_guards::AllowExternal`, then "illegal function call").
- Overrides work per function: tag the definition `PORT_OVERRIDE` in `port/src`, and `unit_copy.py` strips the retail definition (docs/PC.md "How `port/src` takes precedence"). To retire the interpreter, override every member and the free helpers, but keep `rsGetStackInt` and `rsSetStack`.
- `script_guards` is in `port/src/script_guards.{hpp,cpp}`. It is also used by `event.cpp:118` (`GuardEventExternals`), `scriptinterpreter.cpp` and `command_replay.cpp`.
- `port/src/event.cpp:100-119` copies the event table and patches handler pointers before calling `ext_func`.
- Action, effect and monster tables are registered at `actscript.cpp:2617`, `effscript.cpp:786` and `5567`, and `event_func.cpp:2002` and `21813`.

### Loading and data

- `.stb` consumers in the port:
  - `dataread.cpp:226`: `ConvertScriptTiming` for every key ending in `.stb`
  - `dataread.cpp:452`: `DataReadScriptKeys`
  - `effscript.cpp:354/377/408`: `dungeon/eff_script/<name>.stb`
  - `monster.cpp:62`: `dungeon/monster/<name>.stb`
  - `tools/kazarov/port/platform/model_assets.cpp:2440-2456`: character scripts, returned as raw bytes
  - `dataread_port.hpp:56`: startup dry run of every `.stb`
- Script loading sites (partly verified):
  - **Action.** `CActionChara::LoadActionFile` (`ps2/src/actionchara.cpp:3210`) copies the bytes and calls `SetActionScript` (`actscript.cpp:2613`, 0x80 stack slots and 0x200 calls).
    - `InitScript` (`:3219`) runs INIT, then sets MAIN.
    - Each frame, `CActionChara::RunScript` (`:3248`) runs `prog_no` when `check_program` passes, otherwise resumes; when `end` is set it returns to MAIN. Its caller is `dng_main.cpp:2402`.
    - The paths come from `charasetup.cpp:219-221` (`mainchr/c01.stb`), `289-291` (`c02.stb`), `336-338` (`robo.stb`) and `355-365` (`dungeon/act_script/%s`, the monster model name), and from `menuchr.cpp` (5432-6127, `LoadFileBG` with built names; 5693 robo).
  - **Event.**
    - `EventLoop` (`ps2/src/event.cpp:282`) builds the path from `GetMapPath` and the name the script wrote, then tries `LoadFile2(file_path)` and then the directory.
    - `EntryEventScript` (`dng_main.cpp:~940-980`) loads `map/d/d0N/d0N_<lang>.stb` and its variants.
    - `mapjump.cpp` `LoadScript` (`:~390-436`) tries `<base>_<lang>.stb`, then the plain name; when neither loads it calls `SetEventScript(NULL, ...)`.
    - `script_guards.cpp:179-196` and `event_func.cpp:5937` append `_1`.
    - The port overrides `SetEventScript` at `port/src/event.cpp:96`.
  - **Event arguments.** `CEventScriptArg::BuildArgData` (`event_func.cpp:1961`) uses a local `CRunScript` and calls `run(100)`.
  - **Monster.** The port's `LoadReferMonsterFile` (`port/src/monster.cpp:28-74`) loads `dungeon/monster/%s.stb`. `SetMonsterScript` is at `runscript_opcodes.cpp:4676`, and the run and resume are at `runscript_opcodes.cpp:91-99`.
  - **Effect.** The port overrides `EffectScript(path)` (`port/src/effscript.cpp:183`, datapacks plus `script_timing::Apply`), `BuildPack` (`:326`), `LoadBaseEffSpt` (`:358`), `BuildBase` (`:381`) and `GetNeedFilePath` (`:402`). Retail `BuildPack` is at `effscript.cpp:644`.
  - **File layer.** `LoadFile` and `LoadFile2` (`port/src/dataread.cpp:738/746`) call `ResolveFile` (`~:260`), which runs `ConvertScriptTiming` on every `.stb`. `Lookup` (`:150`) checks the index, then `pack.json`, then datapacks.
  - The port does not override the action path (`LoadActionFile`, `SetActionScript`). `SetEffectScript` (`effscript.cpp:5562`: 0x40 stack slots, 2 calls) is called from `ps2/src/effscript.cpp:787` and `port/src/effscript.cpp:128`. `SetMonsterScript` is called from `monster.cpp:1251`, and the per-frame callers are `monster.cpp:2726` and `2811`. The other action callers of `InitScript` are `menuchr.cpp:2802, 4248, 6412` and `menusys.cpp:6844`.
  - `DataReadIndex` (`port/src/dataread.cpp:358`) mounts `<root>/../data.zip`, or `{data_root, <root>/../port_data}`. It then loads datapacks and calls `BuildIndex` (`:331`). The run-dir defaults are in `port/src/main.cpp:66`. `SaveSlotsRoot()` is `PathsSaveRoot() / "saves"` (`save_slots.cpp:325`).
  - Not yet checked: where `PathsSaveRoot`/`PathsDataRoot` are defined, and how `GetPackFile` parses effect pack members.
- Publication goes through the Kazarov extract pipeline in `tools/kazarov/src/rust/core.rs:335-367`: `native_scripts::convert_tree` migrates string operands (`migrate()`, `native_scripts.rs:341-394`), and `verify_tree` checks the result.
- `.stb` files reach `data.zip` through two routes:
  - the passthrough at `textures.rs:1003-1008`, as `data/<path>`
  - `models.rs:2944-2953/3038-3043`, as `port_data/<native path>`, listed in the character's `game.json` `resources.scripts`
- Changes to stop publishing `.stb` would touch those sites, `configs.rs:830` and the `publish.rs` tests.
- `tools/kazarov` is a submodule pinned at `c4745a7`. Its checkout already has **another session's uncommitted edits**, including `native_scripts.rs`. docs/PC.md "The shared-code pin": changes land in the Kazarov main line first, both games must pass on that commit, and only then is the gitlink moved. A game never edits the submodule checkout in place.
- Existing subcommands: `extract`, `list`, `sound`, `check`, `pack` and `current`; anything else prints the usage and exits 2.
- The bridge also exposes `setup_data`, `archive_entry_name`, `archive_entry_size`, `archive_entry_crc`, `archive_entry_stored`, `archive_entry_data_offset`, `archive_read_entry` (CRC-checked) and `png_decode_rgba`. `export-scripts` can read the published scripts with these.
- Other port code that asks for `.stb` by exact path and must be redirected to Luau:
  - `dataread.cpp:452` (`DataReadScriptKeys`)
  - `effscript.cpp:354, 377, 408`
  - `monster.cpp:62`
  - `platform/model_assets.cpp:2440-2456`
  - the hard-coded paths in `script_timing_inventory.inc`
- `tools/dc2data/main.cpp:42-93` dispatches subcommands through an if-chain. It calls the cxx bridge in `tools/kazarov/src/rust/lib.rs`. `export-scripts` needs a new bridge function, or it can use the existing `open_archive`/`archive_read_entry` bridge with the migration applied at extraction.

### Build

- Windows has no CMake presets. `build.bat windows-x64` runs `tools/windows/configure.ps1` with llvm-mingw clang and Ninja from `.kazarov/windows/deps` into `port/build/windows-x64` (Debug, tests off). The test build is `port/build/windows-x64-tests` (docs/PC.md "Windows").
- `port/CMakeLists.txt` includes every `port/cmake/*.cmake` through a glob, so a new `luau.cmake` there is picked up automatically.
- Pinned dependencies use `FetchContent_Declare(... URL ... URL_HASH SHA256=... DOWNLOAD_EXTRACT_TIMESTAMP TRUE)`, as in `tools/kazarov/cmake/KazarovGltf.cmake`.
- `ninja`, `clang`, `7z`, `stylua` and `luau` are not on the shell PATH. Python 3.11.9 is.

### Action corpus

- 27 files: `dungeon/act_script/*.stb` (24) and `mainchr/c01.stb`, `c02.stb` and `c02.stb_`.
- Usage: 78 registered commands used, 6 unregistered numbers called, and 3,894 EXT sites.
- No skip mode; 277 WAITs.
- Per-command arities: `tools/luau/commands.json` (kind `action`).

## Decisions (binding, from the user)

1. Sources go in `port/src/luau/<original path>.luau`, written as readable native Luau, and ship loose in `<game root>/scripts` (dev: `port/run/scripts`), copied by the build or install.
2. Vendor luau-lang/luau as a pinned FetchContent tarball with SHA-256 in `port/cmake/luau.cmake`. Enable Luau.CodeGen (JIT) where supported.
3. Transpile the NTSC-normalized scripts: port the `script_timing` recipes to Python and assert the inventory counts.
4. Add `dc2data export-scripts <dir>`. The survey and the transpiler read its output instead of 7-Zip.
5. Auto-generated helper names are acceptable.
6. No `.stb` bytes in extraction or publication. The file layer maps `.stb` requests to Luau modules.
7. Use native Luau numbers, with helpers only where integer or float semantics differ observably.
8. Type aliases `s8 u8 s16 u16 s32 u32 f32` go in `port/src/luau/api/*.d.luau`. PRINT becomes `print`.

Coordinator design choices so far (not yet implemented):

- `require` aliases go in `port/src/luau/.luaurc` (`@lib/...`).
- `vm` should be a Luau library, so the harness and the game share it.
- The differential harness runs Luau through a small C++ host built against the vendored Luau. The luau CLI sandboxes each required module's globals, which makes mocking the API namespaces awkward.

## Next steps

### M1, transpiler (`tools/luau/stb2luau/`)

1. `refvm.py`: a line-for-line port of `exe` with skip mode, using the semantics above, plus fixtures from `assembler.py`.
2. `timing.py`: the eight `script_timing` recipes on decoded programs, with the inventory moved from `port/src/script_timing_inventory.inc` into data the Python reads, and count assertions.
3. Lift each basic block's stack into expressions, then structure the CFG (if/elseif, while, repeat, numeric for, break, and `and`/`or` from short-circuit jumps).
4. Name and type from `analysis.py` and the catalog, then emit, StyLua (pinned) and `luau-analyze --mode strict`.
5. Differential harness: the refvm and Luau under a mock API with seeded outputs. Compare traces, wait counts, globals and print output. Gate: 27 of 27 action scripts trace-equal.

### M2, runtime

1. `port/cmake/luau.cmake`: VM, Compiler, CodeGen and Analysis (the last for tools).
2. `CRunScript` facade: override every member in `port/src/runscript.cpp`, keeping state in a side registry.
3. Binding generator from `commands.json`, with typed shims that call retail handlers through `RS_STACKDATA`, behind `script_guards`.
4. Loading from `scripts/`, with an interrupt budget and tracebacks.
5. Copy `port/src/luau` to `<run dir>/scripts`.
6. `dc2data export-scripts`, and dropping `.stb` from publication through the Kazarov process.
7. Verify with headless replays and `Smoke.*`, and check that the PS2 build stays byte-identical.

### Later coordinators

M3 (effects and monsters), M4 (events: skip mode, argument scripts, dynamic names), M5 (naming curation), M6 (retire `script_timing` and the interpreter, CI gates). See the plan's milestone table.

## Resuming

Start a coordinator with this file, `docs/PORT_LUAU.md` (Runtime contract) and the plan. Confirm the action-script loading sites (`CActionChara`, `SetActionScript`) first, and finish the loading-site census, since the census was interrupted. Then dispatch M1 steps 1-2 and the M2 step 1 vendoring in parallel. They touch disjoint files.
