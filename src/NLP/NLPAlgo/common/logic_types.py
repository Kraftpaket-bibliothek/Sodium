# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/common/logic_types.py
# Description: Core domain models, enums, and AST representations for
#              synthesizing propositional logic from natural language & math.
# ============================================================================

from __future__ import annotations
from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional, Set, Tuple


class LogicalOp(Enum):
    FACT = auto()     # Atomic proposition
    NOT = auto()      # Negation
    AND = auto()      # Conjunction
    OR = auto()       # Disjunction
    IMPLIES = auto()  # Implication
    EQUIV = auto()    # If and only if


class InvalidLogicalExpressionError(ValueError):
    """Raised when an intermediate logical expression is structurally invalid."""


@dataclass(frozen=True)
class LogicalExpression:
    """Validated expression-tree node used between parsing and CNF compilation."""

    op: LogicalOp
    operands: Tuple["LogicalExpression", ...] = ()
    proposition: Optional[str] = None
    source: str = ""
    start: Optional[int] = None
    end: Optional[int] = None

    def validate(self) -> None:
        if not isinstance(self.op, LogicalOp):
            raise InvalidLogicalExpressionError(f"Unsupported ILR operator: {self.op!r}.")
        if not isinstance(self.operands, tuple):
            raise InvalidLogicalExpressionError("Expression operands must be an immutable tuple.")
        if self.op == LogicalOp.FACT:
            if not isinstance(self.proposition, str) or not self.proposition.strip() or self.operands:
                raise InvalidLogicalExpressionError("An atom must have a non-empty proposition and no operands.")
            return

        if self.proposition is not None:
            raise InvalidLogicalExpressionError(f"{self.op.name} nodes cannot carry a proposition string.")

        if self.op == LogicalOp.NOT:
            if len(self.operands) != 1:
                raise InvalidLogicalExpressionError("NOT requires exactly one operand.")
        elif self.op in (LogicalOp.AND, LogicalOp.OR):
            if len(self.operands) < 2:
                raise InvalidLogicalExpressionError(f"{self.op.name} requires at least two operands.")
        elif self.op in (LogicalOp.IMPLIES, LogicalOp.EQUIV):
            if len(self.operands) != 2:
                raise InvalidLogicalExpressionError(f"{self.op.name} requires exactly two operands.")
        else:
            raise InvalidLogicalExpressionError(f"Unsupported ILR operator: {self.op!r}.")

        for child in self.operands:
            if not isinstance(child, LogicalExpression):
                raise InvalidLogicalExpressionError("Every operand must be a LogicalExpression node.")
            child.validate()

    def atoms(self) -> Set[str]:
        self.validate()
        if self.op == LogicalOp.FACT:
            return {self.proposition or ""}
        result: Set[str] = set()
        for child in self.operands:
            result.update(child.atoms())
        return result
