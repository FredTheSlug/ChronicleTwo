import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from stb2luau import ir  # noqa: E402
from stb2luau.emit import FunctionInput, emit_library  # noqa: E402
from stb2luau.transpile import canonical_key, shareable_blocks  # noqa: E402

CATALOG = {"kinds": {"action": {"namespace": "action", "commands": []}}}


def var(n):
    return ir.Var(("local", n), ir.INT)


def const(value):
    return ir.Const(value, ir.INT)


def function(block, body, *, arg=0, slots=None, programs=(), main=False):
    return FunctionInput(block, arg, 0, dict(slots or {}), [], list(body), list(programs), main)


def call(block, *args):
    return ir.Call(block, tuple(args), ir.INT)


class CanonicalKeyTest(unittest.TestCase):
    def test_offsets_are_ignored(self):
        first = function(0x10, [ir.Assign(("local", 0), const(1), offset=0x100)], arg=1)
        second = function(0x20, [ir.Assign(("local", 0), const(1), offset=0x900)], arg=1)
        self.assertEqual(canonical_key(first, {0x10: first}), canonical_key(second, {0x20: second}))

    def test_different_callee_bodies_give_different_keys(self):
        callee_a = function(0x30, [ir.Print((const(1),))])
        callee_b = function(0x30, [ir.Print((const(2),))])
        caller = function(0x10, [ir.CallStmt(call(0x30))])
        keys_a = canonical_key(caller, {0x10: caller, 0x30: callee_a})
        keys_b = canonical_key(caller, {0x10: caller, 0x30: callee_b})
        self.assertNotEqual(keys_a, keys_b)

    def test_call_names_callee_by_key_not_block(self):
        callee = function(0x30, [ir.Print((const(1),))])
        caller_a = function(0x10, [ir.CallStmt(call(0x30))])
        caller_b = function(0x40, [ir.CallStmt(call(0x50))])
        other = function(0x50, [ir.Print((const(1),))])
        self.assertEqual(
            canonical_key(caller_a, {0x10: caller_a, 0x30: callee}),
            canonical_key(caller_b, {0x40: caller_b, 0x50: other}),
        )


class ShareableTest(unittest.TestCase):
    def test_pure_helper_is_shareable(self):
        helper = function(0x10, [ir.Print((var(0),))], arg=1, slots={("local", 0): ir.INT})
        self.assertEqual(shareable_blocks({0x10: helper}), {0x10})

    def test_global_write_makes_helper_unshareable(self):
        helper = function(0x10, [ir.Assign(("global", 0), const(1))])
        self.assertEqual(shareable_blocks({0x10: helper}), set())

    def test_global_read_makes_helper_unshareable(self):
        helper = function(0x10, [ir.Print((ir.Var(("global", 1), ir.INT),))])
        self.assertEqual(shareable_blocks({0x10: helper}), set())

    def test_global_use_in_a_callee_makes_caller_unshareable(self):
        leaf = function(0x30, [ir.Print((ir.Var(("global", 2), ir.INT),))])
        caller = function(0x10, [ir.CallStmt(call(0x30))])
        self.assertEqual(shareable_blocks({0x10: caller, 0x30: leaf}), set())

    def test_main_and_program_entries_are_not_shareable(self):
        main = function(0x10, [ir.Print((const(1),))], main=True)
        program = function(0x20, [ir.Print((const(1),))], programs=[3])
        self.assertEqual(shareable_blocks({0x10: main, 0x20: program}), set())

    def test_caller_of_a_program_entry_is_not_shareable(self):
        program = function(0x30, [ir.Print((const(1),))], programs=[3])
        caller = function(0x10, [ir.CallStmt(call(0x30))])
        self.assertEqual(shareable_blocks({0x10: caller, 0x30: program}), set())


class EmitLibraryTest(unittest.TestCase):
    def test_tiny_helper_returns_itself(self):
        helper = function(0x10, [ir.Print((var(0),))], arg=1, slots={("local", 0): ir.INT})
        module = emit_library("helper_abcd1234", "action", helper, {0x10: helper}, {}, False, 3, CATALOG)
        self.assertEqual(
            module.text,
            "--!strict\n"
            "-- Shared action helper used by 3 scripts, translated by tools/luau/stb2luau.\n"
            "\n"
            "--- Helper function.\n"
            "local function helper_abcd1234(arg0: s32)\n"
            "\tprint(arg0)\n"
            "end\n"
            "\n"
            "return helper_abcd1234\n",
        )

    def test_callee_is_required_from_its_library(self):
        callee = function(0x30, [ir.Print((var(0),))], arg=1, slots={("local", 0): ir.INT})
        helper = function(0x10, [ir.CallStmt(call(0x30, var(0)))], arg=1, slots={("local", 0): ir.INT})
        module = emit_library(
            "helper_abcd1234",
            "action",
            helper,
            {0x10: helper, 0x30: callee},
            {0x10: "helper_abcd1234", 0x30: "helper_11112222"},
            True,
            2,
            CATALOG,
        )
        self.assertIn('local helper_11112222 = require("@lib/action/helper_11112222")\n', module.text)
        self.assertIn("\thelper_11112222(arg0)\n", module.text)
        self.assertIn("\nreturn helper_abcd1234\n", module.text)

    def test_return_value_is_kept_when_used(self):
        helper = function(0x10, [ir.Return(const(7))])
        module = emit_library("helper_abcd1234", "action", helper, {0x10: helper}, {}, True, 2, CATALOG)
        self.assertIn("\treturn 7\n", module.text)

    def test_uncalled_callee_outside_the_libraries_is_rejected(self):
        callee = function(0x30, [ir.Print((const(1),))])
        helper = function(0x10, [ir.CallStmt(call(0x30))])
        from stb2luau.emit import EmitError

        with self.assertRaises(EmitError):
            emit_library("helper_abcd1234", "action", helper, {0x10: helper, 0x30: callee}, {}, False, 2, CATALOG)


if __name__ == "__main__":
    unittest.main()
