"""Exact Tseitin compiler from validated ILR expressions to DIMACS clauses."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Mapping, Sequence

from .common.logic_types import InvalidLogicalExpressionError, LogicalExpression, LogicalOp
from .entity_extraction.distance_matcher import EntityResolver


class CNFCompilationError(ValueError):
    """Raised when a validated expression cannot be compiled without guessing."""


@dataclass(frozen=True)
class CNFCompilationResult:
    clauses: List[List[int]]
    variable_mapping: Dict[str, str]
    assumptions: List[int]


class LogicalToCNFCompiler:
    """Compiles ILR without interpreting proposition text or using heuristics."""

    def compile(
        self,
        expressions: Sequence[LogicalExpression],
        atom_ids: Mapping[str, int],
        variable_mapping: Mapping[str, str],
    ) -> CNFCompilationResult:
        if not expressions:
            raise CNFCompilationError("Cannot compile an empty set of constraints.")

        try:
            for expression in expressions:
                if not isinstance(expression, LogicalExpression):
                    raise InvalidLogicalExpressionError("Compiler input must contain LogicalExpression nodes.")
                expression.validate()
        except InvalidLogicalExpressionError as exc:
            raise CNFCompilationError(f"Invalid ILR: {exc}") from exc

        normalized_ids: Dict[str, int] = {}
        used_ids = set()
        for atom, var_id in atom_ids.items():
            key = EntityResolver.canonical_key(atom)
            if key in normalized_ids:
                raise CNFCompilationError(f"Duplicate canonical proposition identity {key!r}.")
            if not isinstance(var_id, int) or isinstance(var_id, bool) or var_id <= 0:
                raise CNFCompilationError(f"Invalid variable identifier for proposition {atom!r}.")
            if var_id in used_ids:
                raise CNFCompilationError(f"Duplicate variable identifier {var_id}.")
            normalized_ids[key] = var_id
            used_ids.add(var_id)

        mapping = dict(variable_mapping)
        for var_id in used_ids:
            if str(var_id) not in mapping:
                raise CNFCompilationError(f"Missing variable mapping for identifier {var_id}.")

        self.atom_ids = normalized_ids
        self.variable_mapping = mapping
        self.next_aux_id = max(used_ids, default=0) + 1
        self.aux_counter = 1
        self.clauses: List[List[int]] = []

        try:
            for expression in expressions:
                literal = self._literal_for(expression)
                self.clauses.append([literal, 0])
        except CNFCompilationError:
            raise
        except Exception as exc:
            raise CNFCompilationError(f"CNF conversion failed: {exc}") from exc

        if not self.clauses:
            raise CNFCompilationError("CNF conversion produced no clauses.")
        return CNFCompilationResult(self.clauses, self.variable_mapping, [])

    def _literal_for(self, expression: LogicalExpression) -> int:
        op = expression.op
        if op == LogicalOp.FACT:
            key = EntityResolver.canonical_key(expression.proposition or "")
            if key not in self.atom_ids:
                raise CNFCompilationError(f"No resolved variable exists for proposition {expression.proposition!r}.")
            return self.atom_ids[key]
        if op == LogicalOp.NOT:
            return -self._literal_for(expression.operands[0])

        operands = [self._literal_for(child) for child in expression.operands]
        if op == LogicalOp.AND:
            return self._define_and(operands)
        if op == LogicalOp.OR:
            return self._define_or(operands)
        if op == LogicalOp.IMPLIES:
            return self._define_or([-operands[0], operands[1]])
        if op == LogicalOp.EQUIV:
            left_to_right = self._define_or([-operands[0], operands[1]])
            right_to_left = self._define_or([-operands[1], operands[0]])
            return self._define_and([left_to_right, right_to_left])
        raise CNFCompilationError(f"Unsupported ILR operator {op!r}.")

    def _new_auxiliary(self) -> int:
        var_id = self.next_aux_id
        self.next_aux_id += 1
        name = f"__tseitin_{self.aux_counter}__"
        self.aux_counter += 1
        while name in self.variable_mapping.values():
            name = f"__tseitin_{self.aux_counter}__"
            self.aux_counter += 1
        self.variable_mapping[str(var_id)] = name
        return var_id

    def _define_and(self, operands: Sequence[int]) -> int:
        if len(operands) < 2:
            raise CNFCompilationError("AND requires at least two operands.")
        result = self._new_auxiliary()
        for literal in operands:
            self.clauses.append([-result, literal, 0])
        self.clauses.append([result] + [-literal for literal in operands] + [0])
        return result

    def _define_or(self, operands: Sequence[int]) -> int:
        if len(operands) < 2:
            raise CNFCompilationError("OR requires at least two operands.")
        result = self._new_auxiliary()
        for literal in operands:
            self.clauses.append([-literal, result, 0])
        self.clauses.append([-result] + list(operands) + [0])
        return result
