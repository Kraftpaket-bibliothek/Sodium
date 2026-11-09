# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/common/logic_types.py
# Description: Core domain models, enums, and AST representations for
#              synthesizing propositional logic from natural language & math.
# ============================================================================

from __future__ import annotations
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import List, Dict, Optional, Set, Any


class LogicalOp(Enum):
    FACT = auto()            # Atomic proposition A
    NOT = auto()             # Negation ¬A
    IMPLIES = auto()         # A -> B (¬A ∨ B)
    MUTEX = auto()           # ¬(A ∧ B) (¬A ∨ ¬B)
    EQUIV = auto()           # A <-> B ((¬A ∨ B) ∧ (A ∨ ¬B))
    AND = auto()             # A ∧ B
    OR = auto()              # A ∨ B
    XOR = auto()             # (A ∨ B) ∧ (¬A ∨ ¬B)
    MATH_RELATION = auto()   # e.g. x + y <= 10 (grounded proposition)


@dataclass
class Proposition:
    """Represents a propositional variable in the logic formulation."""
    id: int
    name: str
    description: str = ""
    is_math: bool = False
    raw_expression: str = ""
    polarity: bool = True  # True = positive, False = negated literal

    @property
    def key(self) -> str:
        return str(self.id)


@dataclass
class LogicalRelation:
    """Represents a structured relationship between entities or sub-clauses."""
    op: LogicalOp
    left: List[Proposition]
    right: List[Proposition] = field(default_factory=list)
    raw_text: str = ""
    confidence: float = 1.0


@dataclass
class ParsedRule:
    """Represents a single parsed sentence or requirement line."""
    rule_id: int
    raw_text: str
    normalized_text: str = ""
    entities: List[str] = field(default_factory=list)
    math_tokens: List[str] = field(default_factory=list)
    antecedent: Optional[str] = None
    consequent: Optional[str] = None
    op: LogicalOp = LogicalOp.FACT
    is_mandatory: bool = True
    is_negative: bool = False
    salience_score: float = 1.0


@dataclass
class ClauseAST:
    """Represents a synthesized CNF clause ending with 0."""
    literals: List[int] = field(default_factory=list)
    is_assumption: bool = False
    comment: str = ""

    def to_dimacs_clause(self) -> List[int]:
        c = [lit for lit in self.literals if lit != 0]
        c.append(0)
        return c
