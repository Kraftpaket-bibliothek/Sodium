# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/vector_aggregation/cnf_synthesizer.py
# Description: Synthesizes Conjunctive Normal Form (CNF) clauses and constructs
#              the validated SodiumCNFRequest for CDCL solving.
# ============================================================================

from __future__ import annotations
from typing import List, Dict, Any, Tuple
from ..common.logic_types import LogicalOp, ParsedRule, ClauseAST
from ..entity_extraction.distance_matcher import EntityDistanceMatcher
from ..entity_extraction.pattern_extractor import EntityPatternExtractor
from ..knowledge_injection.antonym_inverter import AntonymPolarityInverter
from ..common.math_shield import MathShield


class CNFSynthesizer:
    """
    Transforms structured ParsedRule abstractions into mathematically sound
    DIMACS CNF clauses adhering strictly to Sodium's Mode 1 JSON contract.
    """

    def __init__(self, distance_matcher: EntityDistanceMatcher):
        self.matcher = distance_matcher
        self.extractor = EntityPatternExtractor()
        self.antonym_inverter = AntonymPolarityInverter()

    def _resolve_literal(self, fragment: str, math_map: Dict[str, str]) -> int:
        """
        Extracts entity name, resolves antonyms, queries distance matcher,
        and returns the signed integer literal (+var_id or -var_id).
        """
        clean_text, is_positive = self.extractor.extract_entity_and_polarity(fragment)

        # Restore shielded math expression if present
        if clean_text in math_map:
            deshielded_name = math_map[clean_text]
            var_id = self.matcher.get_or_register_variable(deshielded_name)
            return var_id if is_positive else -var_id

        # Antonym check: if entity is an antonym of an existing registered entity
        words = clean_text.split('_')
        for w in words:
            antonym = self.antonym_inverter.get_antonym(w)
            if antonym:
                # Check if opposite entity already registered
                opposite_candidate = clean_text.replace(w, antonym)
                if opposite_candidate in self.matcher.canonical_entities:
                    opp_id = self.matcher.canonical_entities[opposite_candidate]
                    # Invert polarity
                    return -opp_id if is_positive else opp_id

        var_id = self.matcher.get_or_register_variable(clean_text)
        return var_id if is_positive else -var_id

    def synthesize_clauses(
        self,
        parsed_rules: List[ParsedRule],
        math_map: Dict[str, str]
    ) -> Tuple[List[List[int]], List[int]]:
        """
        Synthesizes a list of CNF clauses (each ending with 0) and an assumptions list.
        """
        clauses: List[List[int]] = []
        assumptions: List[int] = []

        for rule in parsed_rules:
            # 1. IMPLIES (If A then B -> -A OR B -> [-A, B, 0])
            if rule.op == LogicalOp.IMPLIES and rule.antecedent and rule.consequent:
                lit_a = self._resolve_literal(rule.antecedent, math_map)
                lit_b = self._resolve_literal(rule.consequent, math_map)
                clauses.append([-lit_a, lit_b, 0])

            # 2. MUTEX (A and B cannot coexist -> -A OR -B -> [-A, -B, 0])
            elif rule.op == LogicalOp.MUTEX and rule.antecedent and rule.consequent:
                lit_a = self._resolve_literal(rule.antecedent, math_map)
                lit_b = self._resolve_literal(rule.consequent, math_map)
                clauses.append([-lit_a, -lit_b, 0])

            # 3. EQUIV (A <-> B -> (-A OR B) AND (A OR -B))
            elif rule.op == LogicalOp.EQUIV and rule.antecedent and rule.consequent:
                lit_a = self._resolve_literal(rule.antecedent, math_map)
                lit_b = self._resolve_literal(rule.consequent, math_map)
                clauses.append([-lit_a, lit_b, 0])
                clauses.append([lit_a, -lit_b, 0])

            # 4. OR / UNLESS (A OR B -> [A, B, 0])
            elif rule.op == LogicalOp.OR and rule.antecedent and rule.consequent:
                lit_a = self._resolve_literal(rule.antecedent, math_map)
                lit_b = self._resolve_literal(rule.consequent, math_map)
                clauses.append([lit_a, lit_b, 0])

            # 5. XOR (Exactly one of A or B -> (A OR B) AND (-A OR -B))
            elif rule.op == LogicalOp.XOR and rule.antecedent and rule.consequent:
                lit_a = self._resolve_literal(rule.antecedent, math_map)
                lit_b = self._resolve_literal(rule.consequent, math_map)
                clauses.append([lit_a, lit_b, 0])
                clauses.append([-lit_a, -lit_b, 0])

            # 6. NOT (Unconditional prohibition -> [-A, 0])
            elif rule.op == LogicalOp.NOT and rule.consequent:
                lit = self._resolve_literal(rule.consequent, math_map)
                clauses.append([-abs(lit), 0])

            # 7. FACT (Atomic unit proposition -> [A, 0])
            elif rule.op == LogicalOp.FACT and rule.consequent:
                lit = self._resolve_literal(rule.consequent, math_map)
                if rule.is_negative:
                    clauses.append([-abs(lit), 0])
                else:
                    clauses.append([lit, 0])

        # If no clauses were generated, provide a trivial valid clause
        if not clauses:
            clauses.append([1, 0])

        return clauses, assumptions

    def build_pydantic_payload(
        self,
        clauses: List[List[int]],
        assumptions: List[int],
        description: str = "Synthesized via Classical Non-LLM NLP Pipeline"
    ) -> Dict[str, Any]:
        """
        Constructs the standard Sodium CNF dictionary adhering to SodiumCNFRequest schema.
        """
        mapping = self.matcher.get_variable_mapping()
        if not mapping:
            mapping["1"] = "DefaultEntity"

        return {
            "format": "cnf",
            "solver_config": {
                "name": "sodium-cdcl",
                "timeout_seconds": 10,
                "verbose": False
            },
            "metadata": {
                "description": description,
                "variable_mapping": mapping
            },
            "clauses": clauses,
            "assumptions": assumptions
        }
