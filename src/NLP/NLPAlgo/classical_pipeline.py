"""Coordinator for the controlled-language to CNF compilation pipeline."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

from pydantic import ValidationError

from .common.logic_types import LogicalExpression, LogicalOp
from .entity_extraction.distance_matcher import EntityResolutionError, EntityResolver
from .entity_extraction.domain_matrix import DomainAxisDetector, DynamicDomainMatrix
from .logical_to_cnf import CNFCompilationError, LogicalToCNFCompiler
from .syntactic_parsing.dependency_parser import LogicalDependencyParser, LogicalParseError
from .tokenization_normalization.lemmatizer import LogicLemmatizer
from .tokenization_normalization.math_tokenizer import MathTokenizer, LogicalTokenizationError
from ..cnf_contract import SodiumCNFRequest
from ..solver_bridge import SodiumExecutionBridge


class PipelineStageError(RuntimeError):
    """Compilation error annotated with the stage that rejected the input."""

    def __init__(self, stage: str, detail: str):
        self.stage = stage
        self.detail = detail
        super().__init__(f"{stage} failed: {detail}")


class ClassicalNLPPipeline:
    """Coordinates normalization, parsing, resolution, CNF validation, and solving."""

    def __init__(self, solver_bridge: Optional[SodiumExecutionBridge] = None):
        self.tokenizer = MathTokenizer()
        self.normalizer = LogicLemmatizer()
        self.parser = LogicalDependencyParser()
        self.solver_bridge = solver_bridge

    def compile_text(
        self,
        raw_text: str,
        matrix: Optional[DynamicDomainMatrix] = None,
        additional_clauses: Optional[Sequence[Sequence[int]]] = None,
    ) -> SodiumCNFRequest:
        """Compile one or more controlled-language constraints into the CNF contract."""
        if not isinstance(raw_text, str) or not raw_text.strip():
            raise ValueError("Input validation failed: natural-language input cannot be empty.")

        try:
            rules, math_map = self.tokenizer.split_rules(raw_text)
        except Exception as exc:
            raise PipelineStageError("input normalization", str(exc)) from exc
        if not rules:
            raise PipelineStageError("input normalization", "no non-empty rules were found.")

        expressions: List[LogicalExpression] = []
        for rule in rules:
            try:
                normalized = self.normalizer.expand_contractions(rule)
            except Exception as exc:
                raise PipelineStageError("input normalization", str(exc)) from exc

            try:
                tokens = self.tokenizer.tokenize_expression(normalized)
            except LogicalTokenizationError as exc:
                raise PipelineStageError("tokenization", str(exc)) from exc

            try:
                expression = self.parser.parse(tokens, normalized)
                expressions.append(self._restore_math_atoms(expression, math_map))
            except LogicalParseError as exc:
                raise PipelineStageError("logical parsing", str(exc)) from exc
            except ValueError as exc:
                raise PipelineStageError("ILR validation", str(exc)) from exc

        active_matrix = matrix
        if active_matrix is None:
            detected_cols = DomainAxisDetector.detect_columns(raw_text)
            if detected_cols is not None:
                active_matrix = DynamicDomainMatrix(columns=detected_cols)

        try:
            resolver = EntityResolver(matrix=active_matrix) if active_matrix is not None else EntityResolver()
            atom_ids, variable_mapping = resolver.resolve_many(
                atom for expression in expressions for atom in expression.atoms()
            )
        except EntityResolutionError as exc:
            raise PipelineStageError("entity resolution", str(exc)) from exc

        try:
            result = LogicalToCNFCompiler().compile(
                expressions,
                atom_ids,
                variable_mapping,
                matrix=active_matrix,
                additional_clauses=additional_clauses,
            )
        except CNFCompilationError as exc:
            raise PipelineStageError("CNF conversion", str(exc)) from exc

        try:
            return SodiumCNFRequest(
                metadata={
                    "description": "Compiled by Sodium's controlled Classical NLP pipeline.",
                    "variable_mapping": result.variable_mapping,
                },
                clauses=result.clauses,
                assumptions=result.assumptions,
            )
        except ValidationError as exc:
            raise PipelineStageError("CNF contract validation", str(exc)) from exc

    def solve(self, cnf_request: SodiumCNFRequest) -> Dict[str, Any]:
        """Submit a validated request through the Python-to-C solver bridge."""
        if not isinstance(cnf_request, SodiumCNFRequest):
            raise TypeError("Solver input must be a validated SodiumCNFRequest.")
        bridge = self.solver_bridge or SodiumExecutionBridge()
        return bridge.solve_json(cnf_request.to_json_string())

    @staticmethod
    def _restore_math_atoms(
        expression: LogicalExpression,
        math_map: Dict[str, str],
    ) -> LogicalExpression:
        if expression.op == LogicalOp.FACT:
            proposition = expression.proposition or ""
            restored = math_map.get(proposition, proposition)
            return LogicalExpression(
                op=LogicalOp.FACT,
                proposition=restored,
                source=expression.source,
                start=expression.start,
                end=expression.end,
            )
        return LogicalExpression(
            op=expression.op,
            operands=tuple(
                ClassicalNLPPipeline._restore_math_atoms(child, math_map)
                for child in expression.operands
            ),
            source=expression.source,
            start=expression.start,
            end=expression.end,
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile controlled natural-language logic constraints to CNF and optionally solve."
    )
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument("-t", "--text", type=str, help="Controlled-language constraints.")
    input_group.add_argument("-f", "--file", type=str, help="File containing controlled-language constraints.")
    parser.add_argument("-o", "--output", type=str, help="Write the validated CNF JSON to this path.")
    parser.add_argument("-s", "--solve", action="store_true", help="Solve through the C CDCL bridge.")
    args = parser.parse_args()

    try:
        input_text = args.text if args.text is not None else Path(args.file).read_text(encoding="utf-8")
        pipeline = ClassicalNLPPipeline()
        request = pipeline.compile_text(input_text)
        serialized = request.to_json_string(indent=2)
        if args.output:
            Path(args.output).write_text(serialized, encoding="utf-8")
            print(f"Validated CNF JSON written to {args.output}")
        else:
            print(serialized)
        if args.solve:
            print(json.dumps(pipeline.solve(request), indent=2))
    except (OSError, ValueError, RuntimeError, TypeError) as exc:
        sys.stderr.write(f"Error: {exc}\n")
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
