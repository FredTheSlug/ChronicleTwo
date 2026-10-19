"""Lifts a decoded script function's operand-stack code into expression trees, statements and block exits."""

from __future__ import annotations

import dataclasses
import functools
import math
from collections import defaultdict

from stb.analysis import Analysis, Value, fold, join, wrap32
from stb.opcodes import FLOAT_MODES, GLOBAL_MODES, INDEXED_MODES, JUMPS, POINTER_MODES, AddrMode, ConstType, Op
from stb.opcodes import Compare as CompareOp
from stb.program import Function, Instruction, Program

from . import ir
from .refvm import f32
from .ir import ANY, BOOL, FLOAT, INT, STR

INSTRUCTION_SIZE = 12

_TYPES = {INT: INT, FLOAT: FLOAT, STR: STR, "bool": BOOL, ANY: ANY, "ptr": ANY}
_BINARY = {Op.ADD: "add", Op.SUB: "sub", Op.MUL: "mul", Op.DIV: "div", Op.MOD: "mod", Op.AND: "band", Op.OR: "bor"}
_UNARY = {Op.NEG: "neg", Op.NOT: "not", Op.ITOF: "itof", Op.FTOI: "ftoi", Op.SIN: "sin", Op.COS: "cos"}
_NUMBERS = (INT, BOOL, FLOAT)


class LiftError(ValueError):
    """A script function whose operand-stack code cannot be expressed as structured statements."""


@dataclasses.dataclass
class _Frame:
    """A short-circuit whose right operand is still being lifted."""

    target: int
    op: str
    lhs: ir.Expr
    depth: int


def _static(analysis_type: str) -> str:
    return _TYPES.get(analysis_type, ANY)


def _unary_type(name: str, operand: str) -> str:
    if name in ("itof", "sin", "cos"):
        return FLOAT
    if name in ("not", "ftoi"):
        return INT
    if operand in (INT, BOOL):
        return INT
    return FLOAT if operand == FLOAT else ANY


def _binary_type(name: str, lhs: str, rhs: str) -> str:
    if name in ("mod", "band", "bor"):
        return INT
    if lhs in (INT, BOOL) and rhs in (INT, BOOL):
        return INT
    if FLOAT in (lhs, rhs) and lhs in _NUMBERS and rhs in _NUMBERS:
        return FLOAT
    return ANY


def _fold_float(op: Op, lhs: float, rhs: float) -> float | None:
    if op == Op.DIV and rhs == 0:
        return None
    result = {Op.ADD: lambda: lhs + rhs, Op.SUB: lambda: lhs - rhs, Op.MUL: lambda: lhs * rhs, Op.DIV: lambda: lhs / rhs}.get(op)
    if result is None:
        return None
    value = f32(result())
    return value if math.isfinite(value) else None


def _expressions(statement: ir.Statement) -> tuple[ir.Expr, ...]:
    if isinstance(statement, ir.Assign):
        return (statement.value,)
    if isinstance(statement, (ir.ExtCall, ir.Print)):
        return statement.args
    if isinstance(statement, ir.CallStmt):
        return (statement.call,)
    if isinstance(statement, ir.Return):
        return (statement.value,)
    return ()


def _written(statement: ir.Statement) -> set[ir.Slot]:
    written = {statement.target} if isinstance(statement, ir.Assign) and isinstance(statement.target, tuple) else set()
    for expr in _expressions(statement):
        written.update(sub.slot for sub in ir.walk(expr) if isinstance(sub, ir.Addr))
    return written


def lift(program: Program, function: Function, analysis: Analysis) -> ir.LiftedFunction:
    """Lift one decoded script function into blocks of statements with typed expression trees."""
    lifter = _Lifter(program, function, analysis)
    lifter.run()
    return ir.LiftedFunction(
        block=function.block,
        arg=function.arg,
        local=function.local,
        entry=function.instructions[0].offset,
        blocks=lifter.blocks,
        slot_types=lifter.slot_types,
        temps=lifter.temps,
    )


class _Lifter:
    def __init__(self, program: Program, function: Function, analysis: Analysis):
        self.program = program
        self.function = function
        self.analysis = analysis
        self.facts = analysis.facts[function.block]
        self.graph = analysis.graphs[function.block]
        self.jumps_to: dict[int, list[Instruction]] = defaultdict(list)
        for instruction in function.instructions:
            if instruction.op in JUMPS:
                self.jumps_to[program.jump_target(instruction)].append(instruction)
        self.stack: list[ir.Expr] = []
        self.pending: list[_Frame] = []
        self.blocks: dict[int, ir.LiftedBlock] = {}
        self.current: ir.LiftedBlock | None = None
        self.slot_types: dict[ir.Slot, str] = {}
        self.temps: list[str] = []

    def run(self) -> None:
        for instruction in self.function.instructions:
            offset = instruction.offset
            joined = self._resolve(offset)
            if self.pending or joined:
                # Interior and join points of a short-circuit stay in the current block.
                if offset in self.graph.blocks:
                    jumps = self.jumps_to.get(offset, ())
                    if any(jump.a2 == 0 for jump in jumps) or (not joined and jumps):
                        raise LiftError(f"jump into a short-circuit at {offset:#x}")
            elif self.current is None or self.current.exit is not None or offset in self.graph.blocks:
                self._start_block(offset)
            self._step(instruction)
        if self.pending:
            raise LiftError(f"unresolved short-circuit at {self.pending[-1].target:#x}")
        if self.current is None or self.current.exit is None:
            raise LiftError("function falls off the end")

    def _start_block(self, offset: int) -> None:
        if self.current is not None and self.current.exit is None:
            self._exit(ir.Jump(offset), offset)
        self.current = ir.LiftedBlock(offset, [])
        self.blocks[offset] = self.current

    def _require_flat(self, offset: int) -> None:
        if self.pending:
            raise LiftError(f"statement inside a short-circuit at {offset:#x}")

    def _exit(self, exit_: ir.Jump | ir.Branch | ir.Stop, offset: int) -> None:
        self._require_flat(offset)
        if self.stack:
            raise LiftError(f"value crosses a block boundary at {offset:#x}")
        self.current.exit = exit_

    def _pop(self, offset: int) -> ir.Expr:
        if not self.stack:
            raise LiftError(f"stack underflow at {offset:#x}")
        return self.stack.pop()

    def _pop_many(self, count: int, offset: int) -> tuple[ir.Expr, ...]:
        values = [self._pop(offset) for _ in range(count)]
        values.reverse()
        return tuple(values)

    def _temp(self, type_: str) -> ir.Temp:
        self.temps.append(type_)
        return ir.Temp(len(self.temps) - 1, type_)

    def _slot_type(self, slot: ir.Slot, float_access: bool) -> str:
        if float_access or slot in self.facts.float_slots:
            return FLOAT
        space, n = slot
        if space == "global" and n in self.analysis.global_types:
            return _static(self.analysis.global_types[n])
        types = set(self.facts.stores.get(slot, ()))
        if space == "local" and n < self.function.arg:
            parameter = self.analysis.param_types.get(self.function.block, {}).get(n)
            if parameter is not None:
                types.add(parameter)
        if not types:
            return INT
        return _static(functools.reduce(join, sorted(types)))

    def _emit(self, statement: ir.Statement, offset: int) -> None:
        self._require_flat(offset)
        written = _written(statement)
        calls = any(ir.has_call(expr) for expr in _expressions(statement))
        for i, entry in enumerate(self.stack):
            if isinstance(entry, ir.Addr):
                continue
            reads = ir.reads(entry)
            if ir.has_call(entry) or reads & written or (calls and any(space == "global" for space, _ in reads)):
                temp = self._temp(entry.type)
                self.current.statements.append(ir.Assign(temp, entry, offset))
                self.stack[i] = temp
        self.current.statements.append(statement)

    def _mode(self, instruction: Instruction) -> AddrMode:
        try:
            mode = AddrMode(instruction.a2)
        except ValueError:
            raise LiftError(f"unknown address mode {instruction.a2:#x} at {instruction.offset:#x}") from None
        if mode in POINTER_MODES:
            raise LiftError(f"pointer index at {instruction.offset:#x}")
        return mode

    def _resolve(self, offset: int) -> bool:
        resolved = False
        while self.pending and self.pending[-1].target == offset:
            frame = self.pending.pop()
            if len(self.stack) != frame.depth + 1:
                raise LiftError(f"short-circuit stack mismatch at {offset:#x}")
            rhs = self.stack.pop()
            vtype = BOOL if rhs.type == BOOL else rhs.type
            self.stack.append(ir.Logical(frame.op, frame.lhs, rhs, vtype))
            resolved = True
        return resolved

    def _step(self, instruction: Instruction) -> None:
        op, offset = instruction.op, instruction.offset
        if op == Op.PUSH_CONST:
            self._push_const(instruction)
        elif op in (Op.LOAD, Op.LOAD_ADDR):
            self._load(instruction)
        elif op == Op.STORE:
            value = self._pop(offset)
            target = self._pop(offset)
            if not isinstance(target, ir.Addr):
                raise LiftError(f"store to non-address at {offset:#x}")
            self._emit(ir.Assign(target.slot, value, offset), offset)
            self.stack.append(ir.Var(target.slot, target.type))
        elif op == Op.POP:
            expr = self._pop(offset)
            if isinstance(expr, ir.Call):
                self._emit(ir.CallStmt(expr, offset), offset)
            elif ir.has_call(expr):
                self._emit(ir.Assign(self._temp(expr.type), expr, offset), offset)
        elif op in _BINARY:
            rhs = self._pop(offset)
            lhs = self._pop(offset)
            self._binary(op, lhs, rhs)
        elif op in _UNARY:
            self._unary(op, self._pop(offset))
        elif op == Op.CMP:
            rhs = self._pop(offset)
            lhs = self._pop(offset)
            try:
                comparison = CompareOp(instruction.a1)
            except ValueError:
                raise LiftError(f"unknown comparison {instruction.a1} at {offset:#x}") from None
            self.stack.append(ir.Compare(comparison, lhs, rhs))
        elif op == Op.CALL:
            callee = self.program.callee(instruction)
            args = self._pop_many(callee.arg, offset)
            return_type = _static(self.analysis.return_types.get(callee.block, ANY))
            self.stack.append(ir.Call(callee.block, args, return_type))
        elif op == Op.EXT:
            values = self._pop_many(instruction.a1, offset)
            if not values:
                raise LiftError(f"external call without command at {offset:#x}")
            head = values[0]
            if not (isinstance(head, ir.Const) and head.type == INT):
                raise LiftError(f"dynamic external command at {offset:#x}")
            self._emit(ir.ExtCall(head.value, values[1:], offset), offset)
        elif op == Op.PRINT:
            self._emit(ir.Print(self._pop_many(instruction.a1, offset), offset), offset)
        elif op == Op.WAIT:
            self._emit(ir.Wait(offset), offset)
        elif op == Op.SKIP_END:
            raise LiftError("skip end unsupported")
        elif op == Op.RET:
            self._emit(ir.Return(self._pop(offset), offset), offset)
            self._exit(ir.Stop(), offset)
        elif op == Op.END:
            self._emit(ir.End(offset), offset)
            self._exit(ir.Stop(), offset)
        elif op == Op.JMP:
            self._exit(ir.Jump(self.program.jump_target(instruction)), offset)
        elif op in (Op.JMP_FALSE, Op.JMP_TRUE):
            self._conditional(instruction)

    def _push_const(self, instruction: Instruction) -> None:
        offset = instruction.offset
        if instruction.a1 == ConstType.INT:
            self.stack.append(ir.Const(instruction.a2, INT))
        elif instruction.a1 == ConstType.FLOAT:
            self.stack.append(ir.Const(instruction.a2_float, FLOAT))
        elif instruction.a1 == ConstType.STR:
            self.stack.append(ir.Const(self.program.string(instruction), STR))
        else:
            raise LiftError(f"unknown constant type {instruction.a1} at {offset:#x}")

    def _load(self, instruction: Instruction) -> None:
        offset = instruction.offset
        mode = self._mode(instruction)
        if mode in INDEXED_MODES:
            index = self._pop(offset)
            if not (isinstance(index, ir.Const) and index.type == INT):
                raise LiftError(f"dynamic array index at {offset:#x}")
            base = ("local", instruction.a1)
            slot = ("local", instruction.a1 + index.value)
        else:
            space = "global" if mode in GLOBAL_MODES else "local"
            base = slot = (space, instruction.a1)
        vtype = self._slot_type(base, mode in FLOAT_MODES)
        self.slot_types[slot] = vtype
        if instruction.op == Op.LOAD:
            self.stack.append(ir.Var(slot, vtype))
        else:
            self.stack.append(ir.Addr(slot, vtype))

    def _binary(self, op: Op, lhs: ir.Expr, rhs: ir.Expr) -> None:
        name = _BINARY[op]
        if isinstance(lhs, ir.Const) and isinstance(rhs, ir.Const) and lhs.type == rhs.type == INT:
            folded = fold(op, Value("const", INT, const=lhs.value), Value("const", INT, const=rhs.value))
            if folded is not None:
                self.stack.append(ir.Const(folded.const, INT))
                return
        if isinstance(lhs, ir.Const) and isinstance(rhs, ir.Const) and {lhs.type, rhs.type} <= {INT, FLOAT} and FLOAT in (lhs.type, rhs.type):
            value = _fold_float(op, f32(float(lhs.value)), f32(float(rhs.value)))
            if value is not None:
                self.stack.append(ir.Const(value, FLOAT))
                return
        self.stack.append(ir.Binary(name, lhs, rhs, _binary_type(name, lhs.type, rhs.type)))

    def _unary(self, op: Op, operand: ir.Expr) -> None:
        name = _UNARY[op]
        if op == Op.NEG and isinstance(operand, ir.Const) and operand.type in (INT, FLOAT):
            value = wrap32(-operand.value) if operand.type == INT else -operand.value
            self.stack.append(ir.Const(value, operand.type))
            return
        self.stack.append(ir.Unary(name, operand, _unary_type(name, operand.type)))

    def _conditional(self, instruction: Instruction) -> None:
        offset = instruction.offset
        cond = self._pop(offset)
        target = self.program.jump_target(instruction)
        if instruction.a2:
            op = "and" if instruction.op == Op.JMP_FALSE else "or"
            self.pending.append(_Frame(target, op, cond, len(self.stack)))
            return
        fallthrough = offset + INSTRUCTION_SIZE
        if instruction.op == Op.JMP_FALSE:
            self._exit(ir.Branch(cond, fallthrough, target), offset)
        else:
            self._exit(ir.Branch(cond, target, fallthrough), offset)
