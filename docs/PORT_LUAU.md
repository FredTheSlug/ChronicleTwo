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

## Runtime contract

Draft. The transpiler (M1) and the runtime (M2) must both follow it; change it here first.

- **Modules and instances.** Each script becomes one module, `port/src/luau/<original path without .stb>.luau`, published loose to `<game root>/scripts/`. A module is a chunk executed once per `CRunScript` instance. Its module-level locals are that instance's SB2 globals, which persist across `run()` calls as retail globals do. The chunk returns `{ main = function, [program_no] = function, ... }`.
- **Shared libraries.** Deduplicated helper bodies that are stateless and do not touch globals live in `port/src/luau/lib/<kind>/` and are loaded with `require("@lib/<kind>/<name>")`. `port/src/luau/.luaurc` defines the `lib` alias so `luau-analyze` resolves the same paths as the runtime.
- **Execution.** `run(no)` starts the program's function in a new coroutine and resumes it once. `resume()` resumes it. `vm.wait()` yields. The coroutine's end sets `end`. `check_program(no)` is `programs[no] ~= nil`. An interrupt budget stops loops that never wait. Errors are logged with the original path and a traceback, and the instance ends.
- **Native API.** Commands are called as `<kind namespace>.<CatalogLuauName>(in args...)` with the namespace and name from `tools/luau/commands.json`. Pure outputs come back as multiple results in parameter order. In-out references (handlers that read through the pointer) are passed and also returned. Arity-polymorphic commands accept every catalogued arity. A call to an unregistered command, or with an arity the handler rejects (`handler_rejects`), is a logged no-op, as in retail.
- **Numbers.** Luau numbers are used directly. `vm.idiv`, `vm.imod`, `vm.i32`, `vm.ftoi` and `vm.f32` appear only where C truncation, 32-bit wraparound or float32 rounding is observable.
- **Types.** Annotations use the builtin aliases `s8 u8 s16 u16 s32 u32 f32`, declared in `port/src/luau/api/*.d.luau`. The `PRINT` instruction becomes `print`.
- **Arrays.** By-reference arrays become ordinary Luau tables.
- **Skip mode** (event scripts only). Code between skip ends becomes `vm.section(function() ... end)`; `vm.skip_end()` ends a section.
- **Argument scripts** become data tables.
- **Loading.** No `.stb` bytes are published. The port's file layer maps each `.stb` request (fixed paths, pack members, language fallbacks and names that scripts build) to its Luau module, for example through a synthetic in-memory handle that the overridden `CRunScript::load` recognises.
