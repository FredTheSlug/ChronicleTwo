import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from stb.assembler import assemble  # noqa: E402
from stb.opcodes import AddrMode, Compare  # noqa: E402
from stb.program import decode  # noqa: E402
from stb2luau import harness  # noqa: E402

REPO = HERE.parents[1]
LOCAL = AddrMode.LOCAL
CATALOG = {
    "namespace": "action",
    "commands": [{"no": 2, "luau": "ProgGet", "signatures": [{"argc": 1, "params": [{"name": "prog", "dir": "out", "type": "s32"}]}]}],
}
GOOD_MODULE = """local vm = require("@lib/vm")
local function p100()
    local x = 0
    x = action.ProgGet() or x
    vm.wait()
    print(x)
    return x
end
local script = {}
script.setup = function() end
script[100] = p100
return script
"""
WRONG_MODULE = GOOD_MODULE.replace("print(x)", "print(x + 1)")
STATE_CATALOG = {
    "namespace": "action",
    "commands": [
        {"no": 1, "luau": "ProgSet", "signatures": [{"argc": 1, "params": [{"name": "prog", "dir": "in", "type": "s32"}]}]},
        CATALOG["commands"][0],
    ],
}


def fixture():
    """Program 100 calls command 2 with a local out, waits, prints the local and returns it."""
    body = [
        ("push", 2),
        ("LOAD_ADDR", 0, LOCAL),
        ("EXT", 2),
        ("WAIT",),
        ("LOAD", 0, LOCAL),
        ("PRINT", 1),
        ("LOAD", 0, LOCAL),
        ("RET",),
    ]
    return decode(assemble({"main": (0, 0, [("END",)]), "p100": (1, 0, body)}, "main", {100: "p100"}))


def compare_fixture():
    """Program 101 writes a local through command 2, prints "hit" only when it equals 176, and returns 0."""
    body = [
        ("push", 2),
        ("LOAD_ADDR", 0, LOCAL),
        ("EXT", 2),
        ("LOAD", 0, LOCAL),
        ("push", 176),
        ("CMP", int(Compare.EQ)),
        ("JMP_FALSE", "skip"),
        ("pushs", b"hit"),
        ("PRINT", 1),
        ("label", "skip"),
        ("push", 0),
        ("RET",),
    ]
    return decode(assemble({"main": (0, 0, [("END",)]), "p101": (1, 0, body)}, "main", {101: "p101"}))


def set_then_get_fixture():
    """Program 100 sets 42 through command 1, reads it back through command 2 into a local, prints it and returns 0."""
    body = [
        ("push", 1),
        ("push", 42),
        ("EXT", 2),
        ("push", 2),
        ("LOAD_ADDR", 0, LOCAL),
        ("EXT", 2),
        ("LOAD", 0, LOCAL),
        ("PRINT", 1),
        ("push", 0),
        ("RET",),
    ]
    return decode(assemble({"main": (0, 0, [("END",)]), "p100": (1, 0, body)}, "main", {100: "p100"}))


def float_fixture():
    """Main pushes the float 4.5 and ends."""
    return decode(assemble({"main": (0, 0, [("pushf", 4.5), ("POP",), ("END",)])}, "main"))


class RecordTest(unittest.TestCase):
    def test_record_traces_ext_wait_print_and_end(self):
        program = fixture()
        for seed in range(64):
            scenario = harness.record(program, CATALOG, [100], seed, f"100/s{seed}")
            value = scenario.events[0][4][0]
            if value is not None:
                break
        else:
            self.fail("no seed wrote the out value")
        self.assertIsNone(scenario.inconclusive)
        self.assertEqual(scenario.globals, [])
        self.assertEqual(
            scenario.events,
            [("ext", 2, 1, (), (value,)), ("wait",), ("print", (value,)), ("end", value)],
        )

    def test_pushed_constants_are_output_candidates(self):
        self.assertIn(176, harness.value_pools(compare_fixture()).ints)
        self.assertIn(4.5, harness.value_pools(float_fixture()).floats)

    def test_compared_constant_takes_branch_for_some_seed(self):
        program = compare_fixture()
        runs = [harness.record(program, CATALOG, [101], seed, f"101/s{seed}") for seed in range(64)]
        taken = [run for run in runs if ("print", ("hit",)) in run.events]
        self.assertTrue(taken, "no seed wrote the compared value")
        self.assertLess(len(taken), len(runs))

    def test_handler_rejected_command_is_recorded_without_writes(self):
        catalog = {
            "namespace": "action",
            "commands": [
                {
                    "no": 2,
                    "luau": "ProgGet",
                    "signatures": [{"argc": 1, "handler_rejects": True, "params": [{"name": "prog", "dir": "out", "type": "s32"}]}],
                }
            ],
        }
        scenario = harness.record(fixture(), catalog, [100], 0, "100/s0")
        self.assertEqual(scenario.events, [("ext", 2, 1, (0,), ()), ("wait",), ("print", (0,)), ("end", 0)])

    def test_kind_mismatched_command_is_recorded_without_writes(self):
        catalog = {
            "namespace": "action",
            "commands": [
                {
                    "no": 30,
                    "luau": "SetPallet",
                    "signatures": [
                        {
                            "argc": 2,
                            "params": [
                                {"name": "arg1", "dir": "in", "type": "string"},
                                {"name": "out2", "dir": "out", "type": "s32"},
                            ],
                        }
                    ],
                }
            ],
        }
        body = [("push", 30), ("push", 1), ("LOAD_ADDR", 0, LOCAL), ("EXT", 3), ("push", 0), ("RET",)]
        program = decode(assemble({"main": (0, 0, [("END",)]), "p100": (1, 0, body)}, "main", {100: "p100"}))
        for seed in range(4):
            scenario = harness.record(program, catalog, [100], seed, f"100/s{seed}")
            self.assertEqual(scenario.events, [("ext", 30, 2, (1, 0), ()), ("end", 0)])

    def test_get_mostly_returns_the_value_last_set(self):
        program = set_then_get_fixture()
        runs = [harness.record(program, STATE_CATALOG, [100], seed, f"100/s{seed}", namespace="action") for seed in range(64)]
        for run in runs:
            self.assertEqual(run.events[0], ("ext", 1, 1, (42,), ()))
        read_back = [run.events[1][4][0] for run in runs]
        self.assertGreater(sum(value == 42 for value in read_back), 32, read_back)

    def test_state_is_not_shared_without_a_namespace(self):
        program = set_then_get_fixture()
        runs = [harness.record(program, STATE_CATALOG, [100], seed, f"100/s{seed}") for seed in range(64)]
        self.assertLess(sum(run.events[1][4][0] == 42 for run in runs), 48)

    def test_outputs_are_often_zero(self):
        program = set_then_get_fixture()
        runs = [harness.record(program, STATE_CATALOG, [100], seed, f"100/s{seed}") for seed in range(64)]
        read_back = [run.events[1][4][0] for run in runs]
        self.assertGreater(sum(value == 0 for value in read_back), 16, read_back)

    def test_scenarios_run_each_program_twice_in_one_instance(self):
        recorded = harness.scenarios(fixture(), CATALOG, range(2))
        names = {scenario.name: scenario for scenario in recorded}
        self.assertIn("100x2/s0", names)
        self.assertIn("100x2/s1", names)
        twice = names["100x2/s0"]
        self.assertEqual(twice.programs, [100, 100])
        self.assertEqual([event[0] for event in twice.events].count("end"), 2)
        self.assertEqual([event[0] for event in twice.events].count("ext"), 2)

    def test_straight_line_program_is_fully_covered(self):
        program = fixture()
        self.assertEqual(harness.coverage(program, harness.scenarios(program, CATALOG, range(1))), 1.0)

    def test_unrun_branch_leaves_coverage_partial(self):
        program = compare_fixture()
        runs = [harness.record(program, CATALOG, [101], seed, f"101/s{seed}") for seed in range(64)]
        not_taken = next(run for run in runs if ("print", ("hit",)) not in run.events)
        self.assertLess(harness.coverage(program, [not_taken]), 1.0)
        self.assertEqual(harness.coverage(program, harness.scenarios(program, CATALOG, range(64))), 1.0)


@unittest.skipUnless(harness.HOST.exists(), "luau-harness is not built")
class DifferentialTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        (self.root / "lib").mkdir()
        shutil.copyfile(REPO / "port/src/luau/lib/vm.luau", self.root / "lib" / "vm.luau")

    def write_module(self, source):
        (self.root / "prog.luau").write_text(source, encoding="utf-8")

    def test_matching_module_passes_every_scenario(self):
        self.write_module(GOOD_MODULE)
        result = harness.run(self.root, "prog", fixture(), CATALOG, "action", seeds=range(4))
        self.assertEqual(result["returncode"], 0, result["stderr"])
        self.assertEqual(result["fail"], [])
        self.assertEqual(result["skip"], [])
        self.assertEqual(result["pass"], 4 * 4)
        self.assertEqual(result["coverage"], 1.0)

    def test_wrong_module_fails_scenarios_with_prints(self):
        self.write_module(WRONG_MODULE)
        result = harness.run(self.root, "prog", fixture(), CATALOG, "action", seeds=range(4))
        self.assertEqual(result["returncode"], 0, result["stderr"])
        failed = [line for line in result["fail"] if line.startswith("FAIL 100/s")]
        self.assertEqual(len(failed), 4, result["fail"])
        self.assertEqual(result["pass"], 4)


if __name__ == "__main__":
    unittest.main()
