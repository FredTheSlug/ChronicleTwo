# Luau scripts

The PC port replaces the retail script virtual machine (`CRunScript`, `ps2/src/runscript.cpp`) with Luau. Every compiled script on the disc, whether it carries the `SB2` or the `STB` signature, is a program for that one machine. Scripts differ only in the external-function table registered with the interpreter, so this document names four kinds plus event argument scripts:

| Kind | Scripts | Registration |
|---|---|---|
| `action` | `dungeon/act_script`, `mainchr` | `SetActionScript`, `ps2/src/actscript.cpp` |
| `effect` | `dungeon/eff_script`, `dungeon/articles`, `dungeon/sphida`, `event/tbox/*.pack` | `SetEffectScript`, `ps2/src/effscript.cpp` |
| `monster` | `dungeon/monster` | `SetMonsterScript`, `ps2/src/runscript_opcodes.cpp` |
| `event` | `map`, `dungeon/sys_script`, `event` | `SetEventFunc`, `ps2/src/event_func.cpp` |
| `event_arg` | `*_arg*.stb` | `CEventScriptArg::BuildArgData`, `ps2/src/event_func.cpp` |

The scripts' Luau sources live in `port/src/luau/`, mirroring the original paths. They use Luau type annotations with the port's builtin integer and float aliases (`s8`, `u8`, `s16`, `u16`, `s32`, `u32`, `f32`), and the `PRINT` instruction becomes Luau's `print`.

## Tools

`tools/luau` holds the Python tooling the translation is built from:

- `stb/program.py` decodes a script into its header, program table, functions and instructions. Function discovery matches the publisher and the runtime validator: the main function, the program entries and everything they call, plus compiled helpers that nothing calls.
- `stb/cfg.py` builds each function's control-flow graph with dominators, natural loops and a reducibility test.
- `stb/analysis.py` interprets the operand stack abstractly over the whole program. Integer constants are folded with the machine's 32-bit, truncating semantics, which recovers command numbers that the compiler emits as sums such as `60 + 15`. Variable types are inferred from the float address modes, the values stored into each slot, and the arguments callers pass.
- `stb/sources.py` reads each kind's command enum, registration table and handlers from the decompiled sources. For a handler that reads its arguments in order, the order of its `GetStackInt`, `GetStackFloat`, `GetStackString`, `GetStackVector` and `SetStack` calls gives each argument position a direction, a type and a name.
- `stb/catalog.py` builds the command catalog, and `stb/report.py` renders the corpus report.
- `stb/assembler.py` assembles small programs for test fixtures.

`tools/luau/survey.py` scans the published scripts and writes `tools/luau/commands.json` and `tools/luau/REPORT.md`. It reads `data.zip` through 7-Zip, because the archive's entries are Zstandard-compressed. The data source is `--data` (a data root, a `data.zip`, or a directory of extracted scripts), else `DC2_DATA`, else `port/run`, else the per-user data directory. With `--check`, it writes nothing and exits 1 when either checked-in file is out of date. It exits 77 when no game data is available. Both outputs must be regenerated whenever the handler sources, the curation file or the published data change.

CTest runs the unit tests as `Luau.Tools` and the up-to-date check as `Luau.Survey`, which is skipped without game data.

### Transpiler

`tools/luau/stb2luau` turns the published scripts into the Luau modules under `port/src/luau`. `python tools/luau/stb2luau/transpile.py [--data D] [--kind K] [--only PATH] [--check] [--harness] [--seeds N]` runs it; the data source is found as `survey.py` finds it, and `--kind` defaults to `action`. The stages:

- `timing.py` normalizes each script to NTSC timing before anything else, with the eight `script_timing` recipes and the 17 anchors of `port/src/script_timing.cpp`, rewriting the same instructions to the same markers. The expected counts per script live in `stb2luau/timing_inventory.json`, which a test keeps equal to `port/src/script_timing_inventory.inc` until the C++ is retired. A script whose counts differ from its inventory row, or that is absent from the inventory but matches a recipe, is refused. The corpus check (`DC2_LUAU_CORPUS=1`) confirms every published script matches its row.
- `lift.py` interprets each function's operand stack symbolically into expression trees (`ir.py`). Constant integer and float arithmetic is folded as the PS2 computes it. `JMP_FALSE`/`JMP_TRUE` with a pushed result become `and`/`or`. Values still on the stack are spilled into temporaries before a statement that writes what they read or calls a script function.
- `structure.py` turns each function's blocks into `if`/`elseif`/`else`, `while`, `repeat`, numeric `for`, `break` and `continue`, using the code layout of the retail compiler (every function is reducible).
- `emit.py` prints one `--!strict` module per script, following the Runtime contract below. Locals are declared at the top of each function, typed from the analysis. Outputs take the catalog's parameter names where the handler names them. Programs become handlers on the script table, named from `tools/luau/programs.json` and documented with the enum's comment; `tools/luau/test_programs.py` keeps that file equal to the enums in `ps2/include`. A handler whose whole body calls one argument-free helper is assigned the helper directly (`script.on_death = helper_77acd540`). Other functions are `helper_<offset>`. Truth values are Luau booleans, converted with `if c then 1 else 0` where the machine stores them, and integer conditions test `~= 0`. Integer division, remainder and multiplication of two variables use the `vm` helpers, and bitwise operators use `bit32`. Retail quirks are kept and marked with `-- retail:` comments.
- Helpers that touch no globals and occur in at least two scripts of a kind become `lib/<kind>/helper_<hash>.luau`, named by a hash of their body and callees.
- `api.py` generates `port/src/luau/api/*.d.luau` from the catalog (`python tools/luau/stb2luau/api.py [--check]`).
- The output is formatted with StyLua and checked with `luau-check` (below). `manifest.json` maps each script to its module and the SHA-256 of its normalized bytes, and lists the shared helpers and their users.

The tools are pinned in `tools/luau/toolchain.json` (Luau 0.740 and StyLua 2.5.2 release binaries, with SHA-256), and `stb2luau/toolchain.py` fetches them into `port/build/luau-tools`. `luau-analyze` 0.740 cannot load declaration files, so `tools/luau/harness` (its own CMake project, the same Luau 0.740 tarball) builds `luau-check`, a strict checker that loads `api/types.d.luau` and then the other `api/*.d.luau` before checking, and treats lint warnings as errors, and `luau-harness`, a host that runs a driver script with `require` and an uncached `loadmodule`. Build them with `cmake -S tools/luau/harness -B port/build/luau-harness -G Ninja` and the repo's llvm-mingw.

`--harness` is the differential test. `stb2luau/refvm.py` is a line-for-line Python port of `CRunScript::exe`, including skip mode. `stb2luau/harness.py` runs every program of a script on it: one scenario per program, each program twice on one instance, main, and main followed by every program, per seed. A mock native API answers each call with seeded outputs: unwritten outputs, a zero bias, the script's own constants, and the last `ProgSet` value for `ProgGet`. The harness records the calls, waits, prints, results and final globals, then replays the trace against the Luau module under `luau-harness`, checking every call, its inputs and arity, and returning the recorded outputs. It reports instruction coverage of the reference runs. The reference machine runs in double precision, with float constants spelled as the module spells them (see Numbers).

CTest runs `transpile.py --check --harness` as `Luau.Transpile`, which is skipped without game data or the harness tools.

## Command catalog

`tools/luau/commands.json` is the single description of the native API that both the transpiler and the generated C++ bindings consume. Each kind records its enum, header, registration table and table size, and lists one entry per registered command:

| Field | Meaning |
|---|---|
| `no`, `enum`, `handler` | Command number, enum name and retail handler. |
| `luau` | API name the Luau scripts call, as `<kind namespace>.<luau>`. |
| `description` | The handler's documentation comment. |
| `source` | Handler definition, as `file:line`. |
| `arity_checks` | The handler's checks of its argument count, such as `!= 3`, or the `case` labels of an arity switch. |
| `uses` | Call sites and files in the corpus. |
| `signatures` | One per argument count: `argc`, `sites`, `files` and `params`, plus `handler_rejects` when the handler's checks refuse that count (the machine then prints "illegal function call"), and `review` notes. |

A parameter has a `name`, a direction `dir` (`in` or `out`; an output is passed by reference and written by the handler), and a `type` from the catalog's `types` list, or `unknown`. `observed` counts how call sites supply the position, as a direction and a script-side type. `constants` lists the most common constant arguments, as raw material for enums. `name_source` is `handler` when the name came from the handler's accessor order and `curated` when it came from the curation file. Every other name is positional (`arg1`, `out2`).

Hand curation lives in `tools/luau/curation.json`, keyed by kind and then handler name. An entry may set `luau`, `description` and `notes`, and give `signatures` keyed by argument count, each holding `params` overrides by position (`name`, `type`, `dir`). `"reviewed": true` clears that signature's review notes. The survey applies curation last, so regeneration never loses it.

## Corpus findings

`tools/luau/REPORT.md` holds the counts. The findings that shape the translation:

- All 1,399 scripts decode. No function has an inconsistent operand stack, irreducible control flow or recursion, so every function can be structured into Luau `if`, `while` and `repeat` statements.
- Integer division, integer remainder and 32-bit wraparound need explicit helpers, because Luau's `//` and `%` floor and its numbers are doubles. The report counts the sites of each construct.
- Every CP932 string round-trips through UTF-8, so the Luau sources can hold dialogue and motion names as UTF-8 text.
- Function bodies are heavily shared: event scripts embed a common library, and one body occurs in every file. This makes shared library modules worthwhile.
- A few scripts call command numbers that their kind does not register, or pass argument counts that the handler refuses. In both cases the retail machine prints a diagnostic and continues. The report lists these scripts. For most of them, the majority of calls are unregistered or rejected; such scripts may have been built against an earlier command table. The translation preserves these calls as calls that do nothing.
  - In the action kind these are `dungeon/act_script/c01win.stb` and `f204a.stb`. They call unregistered commands 6, 7, 13, 14, 44 and 80, and pass arities the handlers refuse (`SePlay` with 3 arguments, `GetShotType` with 6, `CheckPause` with 1, `SeLoopPlay` with 0). They also pass a reference where `ProgSet` reads a value (retail then sets the program number to the pointer's bits), and a value where `CheckEquip` writes an output. All of these become `action.Ext(...)` calls, or typed calls whose output is discarded, with `-- retail:` comments. Even `dungeon/act_script/c01.stb` calls `SetTrgAngle` with 0 or 1 arguments, which the handler refuses.

## Luau version

The port and the tools use [Luau 0.740](https://github.com/luau-lang/luau/releases/tag/0.740), fetched by `port/cmake/luau.cmake` from `https://codeload.github.com/luau-lang/luau/tar.gz/refs/tags/0.740` (SHA-256 `419f96e4ecf5a0dd415a2625ea7ea9dc8a407e86d64b25e00936302b25a7afbd`). The transpiler's harness, `luau-analyze` and the game must use this same version; change it here and in `luau.cmake` together.

## Runtime contract

Draft. The transpiler (M1) and the runtime (M2) must both follow it; change it here first.

- **Modules and instances.** Each script becomes one module, `port/src/luau/<original path without .stb>.luau`, published loose to `<game root>/scripts/`. A module is a chunk executed once per `CRunScript` instance. Its module-level locals are that instance's SB2 globals, which persist across `run()` calls as retail globals do.
- **The script table.** The chunk builds and returns a table of handlers: `local script = {}`, `function script.setup() ... end`, `function script.init() ... end`, ..., `return script`.
  - `setup` is the script's top-level entry (retail's main function), run by `run(-1)`.
  - A program the engine runs by number is a field named after it. `tools/luau/programs.json` gives each kind's names: `kinds.<kind>.programs[]` with `no`, `enum`, `name` and `description`, taken from `ACTION_PROG`, `MONSTER_PROG` and `EFFECT_SCRIPT_PROG`. For example, action 100 is `init`, 200 is `loop` and 1400 is `on_death`.
  - A number without a name for its kind is the numeric key `script[n]`.
  - A named program is never also written under its number, and `setup` is never a program name. `*_RUNNING` (-1) has no name.
  - A handler may be any function value, for example `script.on_death = helper`.
  - Only the table's own fields count, so a metatable changes nothing.
  - Presence matters, because the engine asks `check_program` and falls back to its own behaviour when a program is missing. A script defines exactly the programs retail defined. An empty retail program stays as an empty handler, marked "Defined empty, so the engine runs this instead of its default."
- **Shared libraries.** Deduplicated helper bodies that are stateless and do not touch globals live in `port/src/luau/lib/<kind>/` and are loaded with `require("@lib/<kind>/<name>")`. `port/src/luau/.luaurc` defines the `lib` alias so `luau-analyze` resolves the same paths as the runtime.
- **Execution.** `run(no)` maps the number to its handler for the instance's kind (`setup` for a negative number, the `programs.json` name, else `script[no]`), starts it in a new coroutine and resumes it once. `resume()` resumes it. `vm.wait()` yields. The coroutine's end sets `end`. `check_program(no)` reports whether that handler is an own field of the script table. An interrupt budget stops loops that never wait. Errors are logged with the original path, the handler name and number, and a traceback, and the program ends.
- **Native API.** Commands are called as `<kind namespace>.<CatalogLuauName>(in args...)`, with the namespace and name taken from `tools/luau/commands.json`. Pure outputs come back as multiple results, in parameter order. In-out references (handlers that read through the pointer) are passed in and also returned.
  - Every catalogued arity is also available as `<ns>.<Name>_<argc>`. The unsuffixed name picks the arity from the number of inputs. When two catalogued arities take the same number of inputs (for example `action.InitScript` at 0 and 1, or `monster.GetPos` at 1 and 3), the transpiler must use the suffixed name. The unsuffixed name then takes the arity with the most outputs and logs once.
  - `<ns>.Ext(no, args...)` calls command `no` with arguments marshalled by their Luau type: an integral number becomes INT, any other number FLOAT, a string STR, and a boolean 0 or 1. It returns nothing. Use it for command numbers the kind does not register and for arities the catalogue does not list. The retail dispatcher then prints its "not found ext" or "illegal function call" diagnostic, as in retail.
  - An arity the handler rejects (`handler_rejects`) still calls the handler, which refuses it and logs as in retail. The transpiler emits such calls as `<ns>.Ext(no, ...)`, as it does calls whose argument types conflict with the catalogue (a number where the handler reads a string, or the reverse), so the arguments reach the handler with the same bits retail passed.
  - An output the handler does not write comes back as `nil`; retail would leave the script's variable unchanged, so the transpiler assigns `x = r or x` where the old value is observable. The declarations type outputs as `number?` or `string?`.
  - Strings are UTF-8 in Luau and CP932 for the handlers. The converted strings stay alive for the life of the process, because handlers may keep the pointer.
- **Numbers.** Luau numbers are used directly. `vm.idiv`, `vm.imod`, `vm.i32`, `vm.ftoi` and `vm.f32` appear only where C truncation, 32-bit wraparound or float32 rounding is observable. Float constants are written as the shortest decimal that rounds to the script's float32 (`0.68`, not `0.680000007`). Arithmetic runs in double precision. A comparison between an engine float and a literal therefore differs from retail only when the engine value equals the literal's float32 exactly.
- **Types.** Annotations use the builtin aliases `s8 u8 s16 u16 s32 u32 f32`, declared in `port/src/luau/api/*.d.luau`.
- **Printing.** The `PRINT` instruction becomes `print`, with retail's formatting: the arguments are concatenated with no separator and no newline, a string prints as it is, an integral number in the int32 range prints as `%d` and any other number as `%f`, and other values print nothing. Luau numbers carry no int/float tag, so a float holding an integral value prints as an integer; a site that must show the float uses `string.format("%f", x)`.
- **The `vm` library.** `port/src/luau/lib/vm.luau`, loaded with `local vm = require("@lib/vm")`, provides `wait`, `i32`, `ftoi`, `idiv`, `imod`, `f32`, `section` and `skip_end`. `idiv` and `imod` raise an error on a zero divisor, where retail exits.
- **Arrays.** By-reference arrays become ordinary Luau tables.
- **Skip mode** (event scripts only). Code between skip ends becomes `vm.section(function() ... end)`; `vm.skip_end()` ends a section.
- **Argument scripts** become data tables.
- **Loading.** No `.stb` bytes are published. The port's file layer maps each `.stb` request (fixed paths, pack members, language fallbacks and names that scripts build) to its Luau module, for example through a synthetic in-memory handle that the overridden `CRunScript::load` recognises.

## Runtime

`port/src/luau_runtime.{hpp,cpp}` runs the modules, and `port/src/runscript.cpp` routes `CRunScript` to it.

- **Facade.** `CRunScript::load`, `run`, `resume`, `check_program`, `skip` and `DeleteProgram` are overridden. When the loaded program is a Luau handle, they go to the runtime. Otherwise they call the retail bodies, which `scripts/port/unit_copy.py` keeps as `PortRetailLoad`, `PortRetailRun`, `PortRetailResume`, `PortRetailSkip` and `PortRetailDeleteProgram` (declared on the class by `scripts/port/header_fixups.py`). So every kind without modules, and every `.stb` without one, still runs on the interpreter. `ext`, `rsGetStackInt` and `rsSetStack` are unchanged. `load` still assigns the stack and call slots it is given, so the engine's checks see a loaded script.
- **State.** There is one `lua_State`, sandboxed with `luaL_sandbox`, holding the standard libraries, `print`, `require` and one read-only global table per native namespace. Native code generation is enabled when `luau_codegen_supported()` reports it; it does on Windows x64 with llvm-mingw. Each instance is a registry entry keyed by its `CRunScript *`. It holds a thread made with `luaL_sandboxthread`, whose globals table is the instance's, and the program table the module returned. A module file is compiled once (`optimizationLevel` 1, `debugLevel` 1). Each instance runs a clone of the chunk with the thread's globals as its environment, so module locals are per instance. An instance is used only while `prog` still points at the handle it was loaded from, and `load` and `DeleteProgram` (which the constructor calls) forget the old one.
- **Execution.** `run(no)` finds the handler: `setup` for a negative number, the name `programs.json` gives the number for the instance's kind (from its data path: `mainchr/` and `dungeon/act_script/` are `action`), else `script[no]`, all read with raw access. It makes a new coroutine for the handler, resumes it, and returns 1 if it yielded or 0 if it finished, setting `end` and, from a numeric return, `result`. A missing handler returns -1 and logs once. `check_program` makes the same lookup. `resume` and `skip` resume a yielded coroutine. An interrupt callback counts safepoints from each entry and raises an error after 10,000,000 (`SetInterruptBudget`). An error logs `luau: <path>.stb: <handler> (<no>): <message>` with the coroutine's traceback under the `Script` category, and ends the program.
- **Program names.** The build also generates each kind's number-to-name table into `luau_bindings.cpp`, from `tools/luau/programs.json` (`bindings.py --programs`).
- **Native calls.** `tools/luau/bindings.py` turns `tools/luau/commands.json` into constexpr signature tables (`port/src/luau_bindings.hpp`). The build generates them into `<build>/generated/luau/luau_bindings.cpp` for the kinds `CT_LUAU_KINDS` lists in `port/cmake/luau.cmake`, currently `action`. The output is deterministic and rewritten only when it changes. Each `(command, argc)` becomes a C closure.
  - A call builds the slots on the stack: the command number, then INT, FLOAT or STR inputs as the catalogue types them, and PTR slots for outputs, seeded with an unwritten marker. It calls `CRunScript::ext` on the running instance, so `script_guards` and the retail handler run as they do for the interpreter.
  - Outputs return as floats for `f32` and integers otherwise. An `unknown` output is read as a float when its bits are a plausible float.
  - Strings are converted to CP932 with `legacy_encoding::GameText` and interned for the life of the process.
- **Loading.** `ResolveFile` (`port/src/dataread.cpp`) asks `ModuleForScript` about every `.stb` key. For keys under `mainchr/` and `dungeon/act_script/` (the action kind), when `<scripts>/<key without .stb>.luau` exists, it returns a handle instead of the file: `LUAU`, the module key's length and the key, padded to 16 bytes. Every loader then copies the handle as it copied the script (`LoadFile`, `LoadFile2` and `LoadFileBG`, so `CActionChara::LoadActionFile` and the `menuchr.cpp` loads), and the overridden `load` recognises it.
  - `<scripts>` is `<game root>/scripts` when it exists, else `scripts/` beside the executable. The build copies `port/src/luau/**/*.luau` there, without the `.d.luau` declarations, and `cmake --install` installs them to `<bindir>/scripts`.
  - `require("@lib/<name>")` loads `<scripts>/lib/<name>.luau` once per state.
  - `DC2_SCRIPT_VM=interpreter` turns Luau off and runs every script on the interpreter, which is how replays compare the two.
  - The `.stb` files are still published and still validated for NTSC timing at startup, because effect, monster and event scripts still run on the interpreter. Publication stops after event scripts move to Luau (M4).
- **Tools.** `ct_luau_tools` builds `luau-analyze` and `luau-compile` from the same source. `dc2data export-scripts <data.zip or game root> <directory>` writes every published `.stb` at its archive path, the corpus that `tools/luau/survey.py --data <directory>` and the transpiler read without 7-Zip.
- **Tests.** `port/src/tests/luau_runtime_test.cpp` covers the facade with the modules in `port/src/tests/luau/`: run, wait, resume and end, named handlers by kind, per-instance globals, main's result, missing programs, outputs and unwritten outputs, CP932 strings, float inputs, `Ext`, suffixed and ambiguous arities, errors, the interrupt budget, bad modules, `DeleteProgram`, handles, the action key mapping, `print` and the interpreter fallback.

