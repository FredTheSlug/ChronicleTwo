"""Differential testing of transpiled scripts: records reference VM traces and replays them against Luau modules.

Each program of a script runs on the reference VM under a mock native API with seeded outputs, and the events it
produces (native calls, prints, waits and ends) are recorded. The Luau module is then run under a mock that checks
every call, print and yield against the same trace, and each scenario reports PASS, FAIL or SKIP.
"""

from __future__ import annotations

import dataclasses
import math
import random
import subprocess
import sys
import tempfile
from pathlib import Path

from stb.opcodes import ConstType, Op
from stb.program import Program

from . import numbers, refvm
from .emit import NUMERIC_TYPES, SETUP
from .refvm import Slot, Tag

REPO = Path(__file__).resolve().parents[3]
HOST = REPO / "port" / "build" / "luau-harness" / ("luau-harness.exe" if sys.platform == "win32" else "luau-harness")

MAX_STEPS = 500_000
CALL_STACK = 0x200
MAX_DEREF = 8
INT_CHOICES = (0, 0, 1, 1, 2, 3, -1, 7)
FLOAT_CHOICES = (0.0, 0.25, 1.0, -1.0, 2.5, 30.0)
STRING_CHOICES = ("", "a")
# Per kind namespace, the commands that store their first input into a named slot ("set") and the commands whose
# outputs read a named slot back ("get"), so a recorded program number follows the value the script last set.
STATE = {"action": {"set": {1: "prog"}, "get": {2: "prog"}}}
STATE_CHOICES = 4
ZERO_CHOICES = 2


@dataclasses.dataclass
class Scenario:
    """One recorded run: the programs executed in order, the expected event trace and the final globals."""

    name: str
    programs: list[int]
    seed: int
    events: list[tuple]
    globals: list | None
    inconclusive: str | None
    covered: frozenset[int] = frozenset()


@dataclasses.dataclass(frozen=True)
class Pools:
    """Candidate seeded outputs: the base choices followed by the constants a script pushes."""

    ints: tuple[int, ...]
    floats: tuple[float, ...]


def value_pools(program: Program) -> Pools:
    """Output candidates for a program, extended with every integer and finite float constant its functions push."""
    pushed = [
        instruction
        for function in program.functions.values()
        for instruction in function.instructions
        if instruction.op == Op.PUSH_CONST
    ]
    ints = {instruction.a2 for instruction in pushed if instruction.a1 == ConstType.INT}
    floats = {
        numbers.literal(instruction.a2_float)
        for instruction in pushed
        if instruction.a1 == ConstType.FLOAT and math.isfinite(instruction.a2_float)
    }
    return Pools(
        ints=(*INT_CHOICES, *sorted(ints - set(INT_CHOICES))),
        floats=(*FLOAT_CHOICES, *sorted(floats - set(FLOAT_CHOICES))),
    )


def _value(memory: list[Slot], slot: Slot, depth: int = 0) -> object:
    """Script value of a slot, following pointers and decoding strings as cp932."""
    if slot.tag == Tag.PTR:
        if depth == MAX_DEREF:
            return 0
        return _value(memory, memory[slot.value], depth + 1)
    if slot.tag == Tag.STR:
        return slot.value.decode("cp932", errors="replace")
    return slot.value


def _signature(kind_catalog: dict, command: int, argc: int) -> dict | None:
    """Catalogued signature of a command with the given argument count, if any."""
    for entry in kind_catalog["commands"]:
        if entry["no"] == command:
            for signature in entry.get("signatures", []):
                if signature["argc"] == argc:
                    return signature
    return None


def _kind_mismatch(params: list[dict], args: tuple[Slot, ...]) -> bool:
    """Whether an input holds a number where its parameter reads a string, or a string where it reads a number."""
    for param, slot in zip(params, args):
        if param["dir"] != "in":
            continue
        if param["type"] == "string" and slot.tag in (Tag.INT, Tag.FLOAT):
            return True
        if param["type"] in NUMERIC_TYPES and slot.tag == Tag.STR:
            return True
    return False


def _choose(rng: random.Random, type_name: str, pools: Pools) -> object:
    """Seeded output value for a parameter type, drawn from the program's candidates."""
    if type_name == "f32":
        return rng.choice(pools.floats)
    if type_name == "string":
        return rng.choice(STRING_CHOICES)
    return rng.choice(pools.ints)


def _write(machine: refvm.Machine, slot: Slot, value: object) -> object:
    """Store an output through a pointer argument, keeping the target's tag where it applies."""
    if value is None or slot.tag != Tag.PTR:
        return None
    target = machine.read_arg(slot.value)
    if isinstance(value, str):
        stored = Slot(Tag.STR, value.encode("cp932"))
    elif isinstance(value, float):
        stored = Slot(Tag.INT, refvm.float_bits(value)) if target.tag == Tag.INT else Slot(Tag.FLOAT, value)
    else:
        stored = Slot(Tag.FLOAT, refvm.bits_to_float(value)) if target.tag == Tag.FLOAT else Slot(Tag.INT, value)
    machine.write(slot.value, stored)
    return value


class _TracingMachine(refvm.Machine):
    """Reference machine that also appends each PRINT to the scenario trace."""

    def __init__(self, program: Program, *, trace: list[tuple], **kwargs) -> None:
        """Create the machine with the trace it records prints into."""
        super().__init__(program, **kwargs)
        self.events = trace

    def _print(self, count: int) -> None:
        """Print as the base machine does, then record the printed values."""
        super()._print(count)
        base = self.sp
        self.events.append(("print", tuple(_value(self.memory, self.memory[index]) for index in range(base, base + count))))


def record(
    program: Program,
    kind_catalog: dict,
    programs: list[int],
    seed: int,
    name: str,
    resumes: int = 160,
    stack_num: int = 0x80,
    pools: Pools | None = None,
    namespace: str = "",
) -> Scenario:
    """Run the programs in order on the reference VM with seeded native outputs and record the event trace."""
    if pools is None:
        pools = value_pools(program)
    trace: list[tuple] = []
    call_index = 0
    state = STATE.get(namespace, {})
    set_slots: dict[int, str] = state.get("set", {})
    get_slots: dict[int, str] = state.get("get", {})
    held = {slot: 0 for slot in (*set_slots.values(), *get_slots.values())}

    def mock(machine: refvm.Machine, base: int, argc: int) -> None:
        """Answer one native call with seeded outputs and record it."""
        nonlocal call_index
        index = call_index
        call_index += 1
        command = machine.memory[base].value
        count = argc - 1
        args = machine.memory[base + 1 : base + 1 + count]
        values = tuple(_value(machine.memory, slot) for slot in args)
        signature = _signature(kind_catalog, command, count)
        # Unregistered, handler-rejected and kind-mismatched commands write nothing, so their trace keeps every argument.
        if signature is None or signature.get("handler_rejects") or _kind_mismatch(signature["params"], args):
            trace.append(("ext", command, count, values, ()))
            return
        params = signature["params"]
        ins = tuple(values[i] for i, param in enumerate(params) if param["dir"] == "in")
        rng = random.Random(f"{seed}/{name}/{index}")
        outs = []
        for i, param in enumerate(params):
            if param["dir"] != "out":
                continue
            if rng.randrange(8) == 0:
                value = None
            elif command in get_slots and param["type"] != "string" and rng.randrange(STATE_CHOICES) != 0:
                value = held[get_slots[command]]
            else:
                value = _choose(rng, param["type"], pools)
                # Zero keeps most scenarios on the branches a script takes when a flag or count is clear.
                if param["type"] != "string" and rng.randrange(ZERO_CHOICES) == 0:
                    value = 0.0 if param["type"] == "f32" else 0
            outs.append(_write(machine, args[i], value))
        if command in set_slots and ins and isinstance(ins[0], int):
            held[set_slots[command]] = ins[0]
        trace.append(("ext", command, count, ins, tuple(outs)))

    machine = _TracingMachine(
        program,
        trace=trace,
        stack_num=stack_num,
        call_num=CALL_STACK,
        ext=mock,
        float32=False,
        max_steps=MAX_STEPS,
    )
    covered: set[int] = set()
    machine.trace = covered
    inconclusive = None
    try:
        for number in programs:
            machine.result = 0
            if machine.run(number) < 0:
                trace.append(("missing", number))
                continue
            resumed = 0
            while not machine.end:
                trace.append(("wait",))
                if resumed == resumes:
                    trace.append(("suspended",))
                    break
                resumed += 1
                machine.resume()
            else:
                trace.append(("end", machine.result))
    except refvm.VMError as exc:
        if str(exc) == "step budget exceeded":
            inconclusive = str(exc)
        else:
            trace.append(("error",))
    global_num = machine.global_num
    globals_ = [_value(machine.memory, slot) for slot in machine.memory[:global_num]]
    return Scenario(
        name=name,
        programs=list(programs),
        seed=seed,
        events=trace,
        globals=globals_,
        inconclusive=inconclusive,
        covered=frozenset(covered),
    )


def scenarios(program: Program, kind_catalog: dict, seeds: range | list[int] = range(4)) -> list[Scenario]:
    """Record one scenario per program, one running it twice, one for main and one running main then every program, for each seed."""
    numbers = sorted(number for number, _ in program.programs)
    pools = value_pools(program)
    namespace = kind_catalog["namespace"]
    recorded = []
    for seed in seeds:
        for number in numbers:
            recorded.append(record(program, kind_catalog, [number], seed, f"{number}/s{seed}", pools=pools, namespace=namespace))
            recorded.append(
                record(program, kind_catalog, [number, number], seed, f"{number}x2/s{seed}", pools=pools, namespace=namespace)
            )
        recorded.append(record(program, kind_catalog, [-1], seed, f"main/s{seed}", pools=pools, namespace=namespace))
        recorded.append(record(program, kind_catalog, [-1, *numbers], seed, f"all/s{seed}", pools=pools, namespace=namespace))
    return recorded


def _reachable(program: Program) -> set[int]:
    """Blocks of the main function, the programs and every function they call, directly or through other calls."""
    seen: set[int] = set()
    todo = [program.main, *(block for _, block in program.programs)]
    while todo:
        block = todo.pop()
        if block in seen:
            continue
        seen.add(block)
        for instruction in program.functions[block].instructions:
            if instruction.op == Op.CALL:
                todo.append(program.callee(instruction).block)
    return seen


def coverage(program: Program, recorded: list[Scenario]) -> float:
    """Fraction of the instructions of the reachable functions that the recorded scenarios executed."""
    total = {instruction.offset for block in _reachable(program) for instruction in program.functions[block].instructions}
    executed = set().union(*(scenario.covered for scenario in recorded))
    return len(executed & total) / len(total) if total else 0.0


def _string(text: str) -> str:
    """Luau string literal for a Python string."""
    parts = []
    for char in text:
        if char in '"\\':
            parts.append("\\" + char)
        elif 32 <= ord(char) < 127:
            parts.append(char)
        else:
            parts.append("\\u{%x}" % ord(char))
    return '"' + "".join(parts) + '"'


def _lit(value: object) -> str:
    """Luau literal for a recorded value; None becomes nil."""
    if value is None:
        return "nil"
    if isinstance(value, str):
        return _string(value)
    if isinstance(value, float):
        if math.isnan(value):
            return "(0/0)"
        if math.isinf(value):
            return "math.huge" if value > 0 else "-math.huge"
        return repr(value)
    return str(value)


def _list(values: tuple | list) -> str:
    """Luau table literal holding a sequence with an explicit count, so nil entries survive."""
    items = "".join(", " + _lit(value) for value in values)
    return "{ n = %d%s }" % (len(values), items)


def _event(event: tuple) -> str:
    """Luau table literal for one trace event."""
    tag = event[0]
    if tag in ("wait", "suspended", "error"):
        return '{ "%s" }' % tag
    if tag == "print":
        return '{ "print", %s }' % _list(event[1])
    if tag in ("end", "missing"):
        return '{ "%s", %s }' % (tag, _lit(event[1]))
    if tag == "ext":
        _, command, count, ins, outs = event
        return '{ "ext", %d, %d, %s, %s }' % (command, count, _list(ins), _list(outs))
    raise ValueError(f"unknown trace event {event!r}")


def _scenario_literal(scenario: Scenario) -> str:
    """Luau table literal for one recorded scenario."""
    fields = [
        "name = %s" % _string(scenario.name),
        "programs = %s" % _list(scenario.programs),
        "globals = %s" % _list(scenario.globals or []),
        "events = { %s }" % ", ".join(_event(event) for event in scenario.events),
    ]
    if scenario.inconclusive is not None:
        fields.append("inconclusive = %s" % _string(scenario.inconclusive))
    return "{ %s }" % ", ".join(fields)


def _dispatch(name: str, namespace: str, command: dict) -> list[str]:
    """Luau mock functions for every catalogued arity of a command, plus the unsuffixed dispatcher."""
    lines = []
    chosen: dict[int, tuple[tuple[int, int], dict]] = {}
    for signature in command.get("signatures", []):
        argc = signature["argc"]
        params = signature["params"]
        inputs = sum(1 for param in params if param["dir"] == "in")
        outputs = sum(1 for param in params if param["dir"] == "out")
        lines.append(
            f"{namespace}.{name}_{argc} = function(...)\n"
            f"    local outs = ext_call({command['no']}, {argc}, table.pack(...))\n"
            "    return table.unpack(outs, 1, outs.n)\n"
            "end"
        )
        key = (outputs, -argc)
        if inputs not in chosen or key > chosen[inputs][0]:
            chosen[inputs] = (key, signature)
    if not chosen:
        return lines
    lines.append(f"{namespace}.{name} = function(...)")
    lines.append("    local count = select(\"#\", ...)")
    for inputs, (_, signature) in sorted(chosen.items()):
        lines.append(f"    if count == {inputs} then")
        lines.append(f"        return {namespace}.{name}_{signature['argc']}(...)")
        lines.append("    end")
    lines.append(f'    fail("{namespace}.{name}: no arity takes " .. count .. " inputs")')
    lines.append("end")
    return lines


_RUNTIME_HEAD = """local MISMATCH = {}
local events, cursor, failure = {}, 1, nil

-- Renders a trace value for mismatch messages.
local function show(value)
    if type(value) ~= "table" then
        return tostring(value)
    end
    local parts = {}
    for i = 1, value.n or #value do
        parts[i] = show(value[i])
    end
    return "{" .. table.concat(parts, ", ") .. "}"
end

-- Records a mismatch and aborts the running scenario.
local function fail(message)
    failure = message
    error(MISMATCH, 0)
end

-- Compares an expected trace value with the value the script produced.
local function same(expected, got)
    if expected == nil or got == nil then
        return false
    end
    if expected == got then
        return true
    end
    if type(expected) == "number" and type(got) == "number" then
        if expected ~= expected then
            return got ~= got
        end
        local diff = math.abs(expected - got)
        return diff <= 1e-6 or diff <= 1e-5 * math.abs(expected)
    end
    return false
end

-- Compares two counted lists element by element.
local function same_list(expected, got)
    if expected.n ~= got.n then
        return false
    end
    for i = 1, got.n do
        if not same(expected[i], got[i]) then
            return false
        end
    end
    return true
end

-- Takes the next event of the trace.
local function next_event()
    local event = events[cursor]
    cursor = cursor + 1
    return event
end

-- Consumes the next event, which must carry the given tag and field.
local function expect(tag, field)
    local at = cursor
    local event = next_event()
    if event == nil or event[1] ~= tag or (field ~= nil and not same(event[2], field)) then
        local got = field == nil and table.pack(tag) or table.pack(tag, field)
        fail(("event %d: expected %s, got %s"):format(at, show(event), show(got)))
    end
end

-- Checks a native call against the next event and returns the recorded outputs.
local function ext_call(no, argc, args)
    local at = cursor
    local event = next_event()
    if event == nil or event[1] ~= "ext" or event[2] ~= no or event[3] ~= argc or not same_list(event[4], args) then
        fail(("event %d: expected %s, got %s"):format(at, show(event), show({ "ext", no, argc, args })))
    end
    return event[5]
end

-- Checks each printed value against the next event.
print = function(...)
    local values = table.pack(...)
    local at = cursor
    local event = next_event()
    if event == nil or event[1] ~= "print" or not same_list(event[2], values) then
        fail(("event %d: expected %s, got %s"):format(at, show(event), show(table.pack("print", values))))
    end
end
"""

_RUNTIME_TAIL = """
-- Runs one program to its end, checking each wait; returns "errored" when the script raised an error.
local function run_program(fn)
    local co = coroutine.create(fn)
    local ok, result = coroutine.resume(co)
    while true do
        if not ok then
            if result == MISMATCH then
                error(MISMATCH, 0)
            end
            expect("error")
            return "errored"
        end
        if coroutine.status(co) == "dead" then
            if type(result) == "boolean" then
                fail("program returned a boolean")
            end
            if result == nil then
                result = 0
            end
            expect("end", result)
            return "done"
        end
        expect("wait")
        local following = events[cursor]
        if following ~= nil and following[1] == "suspended" then
            cursor = cursor + 1
            return "done"
        end
        ok, result = coroutine.resume(co)
    end
end

-- Runs the scenario's programs in order, then checks the trace is used up and the globals match.
local function execute(scenario, m)
    for i = 1, scenario.programs.n do
        local no = scenario.programs[i]
        local fn = m[HANDLERS[no] or no]
        if fn == nil then
            expect("missing", no)
        elseif run_program(fn) == "errored" then
            break
        end
    end
    if cursor <= #events then
        fail(#events - cursor + 1 .. " events not reached, next is " .. show(events[cursor]))
    end
    if m.__globals ~= nil then
        local got = m.__globals()
        local count = scenario.globals.n
        if (got.n or #got) ~= count then
            fail("expected " .. count .. " globals, got " .. tostring(got.n or #got))
        end
        for i = 1, count do
            if not same(scenario.globals[i], got[i]) then
                fail("global " .. i .. ": expected " .. show(scenario.globals[i]) .. ", got " .. show(got[i]))
            end
        end
    end
end

-- Runs one scenario against a fresh module instance; returns nil on success or the failure message.
local function run_scenario(scenario)
    events, cursor, failure = scenario.events, 1, nil
    resetbudget()
    local ok, err = pcall(function()
        local m = loadmodule(MODULE)
        execute(scenario, m)
    end)
    if ok then
        return nil
    end
    if err == MISMATCH then
        return failure
    end
    return "script error: " .. tostring(err)
end

for _, scenario in ipairs(SCENARIOS) do
    if scenario.inconclusive ~= nil then
        real_print("SKIP " .. scenario.name .. " " .. scenario.inconclusive)
    else
        local message = run_scenario(scenario)
        if message == nil then
            real_print("PASS " .. scenario.name)
        else
            real_print("FAIL " .. scenario.name .. " " .. message)
        end
    end
end
"""


def driver(
    module: str, namespace: str, kind_catalog: dict, scenario_list: list[Scenario], names: dict[int, str]
) -> str:
    """Luau source of the host driver that replays the scenarios against a module."""
    handlers = {SETUP: "setup", **names}
    lines = [
        "-- Generated by tools/luau/stb2luau/harness.py: replays recorded reference VM traces.",
        "local real_print = print",
        f"local MODULE = {_string(module)}",
        _RUNTIME_HEAD,
        f"{namespace} = {{}}",
        "local HANDLERS = {",
        *(f"    [{no}] = {_string(name)}," for no, name in sorted(handlers.items())),
        "}",
    ]
    for command in kind_catalog["commands"]:
        lines.extend(_dispatch(command["luau"], namespace, command))
    lines.append(
        f"{namespace}.Ext = function(no, ...)\n"
        "    local args = table.pack(...)\n"
        "    local outs = ext_call(no, args.n, args)\n"
        "    if outs.n ~= 0 then\n"
        '        fail("ext " .. tostring(no) .. " returns no outputs")\n'
        "    end\n"
        "end"
    )
    lines.append("local SCENARIOS = {")
    lines.extend(f"    {_scenario_literal(scenario)}," for scenario in scenario_list)
    lines.append("}")
    lines.append(_RUNTIME_TAIL)
    return "\n".join(lines)


def run(
    scripts_root: Path,
    module: str,
    program: Program,
    kind_catalog: dict,
    namespace: str,
    seeds: range | list[int] = range(4),
    names: dict[int, str] | None = None,
) -> dict:
    """Record the scenarios, replay them against the module under luau-harness, and summarise the results.

    The names map program numbers to the handler names the module defines them under; other numbers are read as
    script[number].
    """
    recorded = scenarios(program, kind_catalog, seeds)
    source = driver(module, namespace, kind_catalog, recorded, names or {})
    with tempfile.TemporaryDirectory() as directory:
        driver_path = Path(directory) / "driver.luau"
        driver_path.write_text(source, encoding="utf-8")
        completed = subprocess.run(
            [str(HOST), str(scripts_root), str(driver_path)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=600,
            check=False,
        )
    passed = 0
    failed: list[str] = []
    skipped: list[str] = []
    for line in completed.stdout.splitlines():
        if line.startswith("PASS "):
            passed += 1
        elif line.startswith("FAIL "):
            failed.append(line)
        elif line.startswith("SKIP "):
            skipped.append(line)
    return {
        "pass": passed,
        "fail": failed,
        "skip": skipped,
        "coverage": coverage(program, recorded),
        "stderr": completed.stderr,
        "returncode": completed.returncode,
    }
