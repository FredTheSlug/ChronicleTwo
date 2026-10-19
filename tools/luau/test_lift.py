import dataclasses
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from stb.analysis import Analysis  # noqa: E402
from stb.assembler import assemble  # noqa: E402
from stb.opcodes import AddrMode, Compare, Op  # noqa: E402
from stb.program import decode  # noqa: E402
from stb2luau import ir  # noqa: E402
from stb2luau.ir import FLOAT, INT, STR  # noqa: E402
from stb2luau.lift import LiftError, lift  # noqa: E402

LOCAL, LOCAL_INDEX, POINTER_INDEX = AddrMode.LOCAL, AddrMode.LOCAL_INDEX, AddrMode.POINTER_INDEX


def plain(statements):
    """Statements with their instruction offsets cleared, for comparison against expected trees."""
    return [dataclasses.replace(s, offset=-1) if hasattr(s, "offset") else s for s in statements]


def local(n, type_=INT):
    return ir.Var(("local", n), type_)


def const(value, type_=INT):
    return ir.Const(value, type_)


class LiftTest(unittest.TestCase):
    def lift_main(self, functions):
        program = decode(assemble(functions, "main"))
        return program, lift(program, program.functions[program.main], Analysis(program))

    def test_constant_folding(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [("LOAD_ADDR", 0, LOCAL), ("push", 60), ("push", 15), ("ADD",), ("STORE",), ("POP",), ("END",)]),
        })
        block = lifted.blocks[lifted.entry]
        self.assertEqual(plain(block.statements), [ir.Assign(("local", 0), const(75)), ir.End()])
        self.assertIsInstance(block.exit, ir.Stop)

    def test_store_then_pop_is_one_assign(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [("LOAD_ADDR", 0, LOCAL), ("push", 5), ("STORE",), ("POP",), ("END",)]),
        })
        statements = lifted.blocks[lifted.entry].statements
        self.assertEqual(plain(statements), [ir.Assign(("local", 0), const(5)), ir.End()])
        self.assertEqual(lifted.slot_types[("local", 0)], INT)
        self.assertEqual(lifted.temps, [])

    def test_assignment_chain(self):
        _, lifted = self.lift_main({
            "main": (2, 0, [("LOAD_ADDR", 0, LOCAL), ("LOAD_ADDR", 1, LOCAL), ("push", 5), ("STORE",), ("STORE",), ("POP",), ("END",)]),
        })
        statements = lifted.blocks[lifted.entry].statements
        self.assertEqual(plain(statements), [
            ir.Assign(("local", 1), const(5)),
            ir.Assign(("local", 0), local(1)),
            ir.End(),
        ])

    def test_while_loop_blocks_and_branches(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [
                ("LOAD_ADDR", 0, LOCAL), ("push", 0), ("STORE",), ("POP",),
                ("label", "top"),
                ("LOAD", 0, LOCAL), ("push", 3), ("CMP", Compare.LT), ("JMP_FALSE", "end"),
                ("LOAD_ADDR", 0, LOCAL), ("LOAD", 0, LOCAL), ("push", 1), ("ADD",), ("STORE",), ("POP",),
                ("JMP", "top"),
                ("label", "end"), ("END",),
            ]),
        })
        entry, top, body, end = sorted(lifted.blocks)
        self.assertEqual(lifted.blocks[entry].exit, ir.Jump(top))
        self.assertEqual(plain(lifted.blocks[top].statements), [])
        self.assertEqual(lifted.blocks[top].exit, ir.Branch(ir.Compare(Compare.LT, local(0), const(3)), body, end))
        self.assertEqual(plain(lifted.blocks[body].statements), [ir.Assign(("local", 0), ir.Binary("add", local(0), const(1), INT))])
        self.assertEqual(lifted.blocks[body].exit, ir.Jump(top))
        self.assertEqual(plain(lifted.blocks[end].statements), [ir.End()])

    def test_jump_orientation(self):
        _, false_lifted = self.lift_main({
            "main": (0, 0, [("push", 1), ("JMP_FALSE", "end"), ("push", 2), ("POP",), ("label", "end"), ("END",)]),
        })
        entry, fall, end = sorted(false_lifted.blocks)
        self.assertEqual(false_lifted.blocks[entry].exit, ir.Branch(const(1), fall, end))
        self.assertEqual(false_lifted.blocks[fall].exit, ir.Jump(end))

        _, true_lifted = self.lift_main({
            "main": (0, 0, [("push", 1), ("JMP_TRUE", "end"), ("push", 2), ("POP",), ("label", "end"), ("END",)]),
        })
        entry, fall, end = sorted(true_lifted.blocks)
        self.assertEqual(true_lifted.blocks[entry].exit, ir.Branch(const(1), end, fall))

    def test_logical_and_or(self):
        _, lifted = self.lift_main({
            "main": (2, 0, [
                ("LOAD", 0, LOCAL), ("JMP_FALSE", "l", 1), ("LOAD", 1, LOCAL), ("label", "l"),
                ("JMP_FALSE", "out"), ("END",), ("label", "out"), ("END",),
            ]),
        })
        entry, fall, out = sorted(lifted.blocks)
        conditional = ir.Logical("and", local(0), local(1), INT)
        self.assertEqual(lifted.blocks[entry].exit, ir.Branch(conditional, fall, out))

        _, lifted = self.lift_main({
            "main": (2, 0, [
                ("LOAD", 0, LOCAL), ("JMP_TRUE", "l", 1), ("LOAD", 1, LOCAL), ("label", "l"),
                ("JMP_FALSE", "out"), ("END",), ("label", "out"), ("END",),
            ]),
        })
        entry, fall, out = sorted(lifted.blocks)
        self.assertEqual(lifted.blocks[entry].exit, ir.Branch(ir.Logical("or", local(0), local(1), INT), fall, out))

    def test_nested_short_circuit(self):
        _, lifted = self.lift_main({
            "main": (3, 0, [
                ("LOAD", 0, LOCAL), ("JMP_FALSE", "l1", 1), ("LOAD", 1, LOCAL), ("JMP_TRUE", "l2", 1),
                ("LOAD", 2, LOCAL), ("label", "l2"), ("label", "l1"),
                ("JMP_FALSE", "out"), ("END",), ("label", "out"), ("END",),
            ]),
        })
        entry, fall, out = sorted(lifted.blocks)
        condition = ir.Logical("and", local(0), ir.Logical("or", local(1), local(2), INT), INT)
        self.assertEqual(lifted.blocks[entry].exit, ir.Branch(condition, fall, out))
        self.assertEqual(lifted.blocks[entry].statements, [])

    def test_short_circuit_value_stored(self):
        _, lifted = self.lift_main({
            "main": (3, 0, [
                ("LOAD_ADDR", 0, LOCAL), ("LOAD", 1, LOCAL), ("JMP_FALSE", "l", 1), ("LOAD", 2, LOCAL),
                ("label", "l"), ("STORE",), ("POP",), ("END",),
            ]),
        })
        statements = lifted.blocks[lifted.entry].statements
        self.assertEqual(plain(statements), [
            ir.Assign(("local", 0), ir.Logical("and", local(1), local(2), INT)),
            ir.End(),
        ])
        self.assertEqual(len(lifted.blocks), 1)

    def test_external_call_with_address_argument(self):
        _, lifted = self.lift_main({
            "main": (2, 0, [("push", 9), ("LOAD_ADDR", 0, LOCAL), ("push", 7), ("EXT", 3), ("END",)]),
        })
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [
            ir.ExtCall(9, (ir.Addr(("local", 0), INT), const(7))),
            ir.End(),
        ])

    def test_spill_before_external_write(self):
        _, lifted = self.lift_main({
            "main": (2, 0, [
                ("LOAD_ADDR", 1, LOCAL), ("LOAD", 0, LOCAL), ("push", 9), ("LOAD_ADDR", 0, LOCAL),
                ("EXT", 2), ("STORE",), ("POP",), ("END",),
            ]),
        })
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [
            ir.Assign(ir.Temp(0, INT), local(0)),
            ir.ExtCall(9, (ir.Addr(("local", 0), INT),)),
            ir.Assign(("local", 1), ir.Temp(0, INT)),
            ir.End(),
        ])
        self.assertEqual(lifted.temps, [INT])

    def test_call_statement_and_expression(self):
        program, lifted = self.lift_main({
            "f": (1, 1, [("LOAD", 0, LOCAL), ("RET",)]),
            "main": (1, 0, [
                ("push", 4), ("CALL", "f"), ("POP",),
                ("LOAD_ADDR", 0, LOCAL), ("push", 1), ("CALL", "f"), ("STORE",), ("POP",), ("END",),
            ]),
        })
        callee = min(block for block in program.functions if block != program.main)
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [
            ir.CallStmt(ir.Call(callee, (const(4),), INT)),
            ir.Assign(("local", 0), ir.Call(callee, (const(1),), INT)),
            ir.End(),
        ])

    def test_return_and_end_exits(self):
        program, lifted = self.lift_main({
            "f": (1, 1, [("LOAD", 0, LOCAL), ("RET",)]),
            "main": (0, 0, [("push", 0), ("CALL", "f"), ("POP",), ("END",)]),
        })
        callee = min(block for block in program.functions if block != program.main)
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [ir.CallStmt(ir.Call(callee, (const(0),), INT)), ir.End()])
        self.assertIsInstance(lifted.blocks[lifted.entry].exit, ir.Stop)
        callee_lifted = lift(program, program.functions[callee], Analysis(program))
        block = callee_lifted.blocks[callee_lifted.entry]
        self.assertEqual(plain(block.statements), [ir.Return(local(0))])
        self.assertIsInstance(block.exit, ir.Stop)

    def test_dynamic_index_raises(self):
        with self.assertRaisesRegex(LiftError, "dynamic array index at"):
            self.lift_main({
                "main": (2, 0, [("LOAD", 1, LOCAL), ("LOAD", 0, LOCAL_INDEX), ("POP",), ("END",)]),
            })

    def test_constant_index_is_element_slot(self):
        _, lifted = self.lift_main({
            "main": (2, 0, [("push", 1), ("LOAD", 0, LOCAL_INDEX), ("POP",), ("END",)]),
        })
        self.assertIn(("local", 1), lifted.slot_types)
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [ir.End()])

    def test_pointer_index_raises(self):
        with self.assertRaisesRegex(LiftError, "pointer index at"):
            self.lift_main({
                "main": (1, 0, [("push", 0), ("LOAD", 0, POINTER_INDEX), ("POP",), ("END",)]),
            })

    def test_float_constant_folding(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [("LOAD_ADDR", 0, LOCAL), ("pushf", 2.0), ("pushf", 20.0), ("MUL",), ("STORE",), ("POP",), ("END",)]),
        })
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [ir.Assign(("local", 0), const(40.0, FLOAT)), ir.End()])

    def test_mixed_int_float_folding_rounds_to_float32(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [("LOAD_ADDR", 0, LOCAL), ("push", 16777217), ("pushf", 1.0), ("ADD",), ("STORE",), ("POP",), ("END",)]),
        })
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [ir.Assign(("local", 0), const(16777216.0, FLOAT)), ir.End()])

    def test_float_division_by_zero_is_not_folded(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [("LOAD_ADDR", 0, LOCAL), ("pushf", 1.0), ("pushf", 0.0), ("DIV",), ("STORE",), ("POP",), ("END",)]),
        })
        statements = plain(lifted.blocks[lifted.entry].statements)
        self.assertEqual(statements[0], ir.Assign(("local", 0), ir.Binary("div", const(1.0, FLOAT), const(0.0, FLOAT), FLOAT)))

    def test_float_overflow_is_not_folded(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [("LOAD_ADDR", 0, LOCAL), ("pushf", 3.0e38), ("pushf", 3.0e38), ("MUL",), ("STORE",), ("POP",), ("END",)]),
        })
        statements = plain(lifted.blocks[lifted.entry].statements)
        self.assertIsInstance(statements[0].value, ir.Binary)

    def test_string_constant(self):
        _, lifted = self.lift_main({
            "main": (1, 0, [("LOAD_ADDR", 0, LOCAL), ("pushs", b"abc"), ("STORE",), ("POP",), ("END",)]),
        })
        self.assertEqual(plain(lifted.blocks[lifted.entry].statements), [ir.Assign(("local", 0), const(b"abc", STR)), ir.End()])


if __name__ == "__main__":
    unittest.main()
