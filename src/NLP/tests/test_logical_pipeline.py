from __future__ import annotations

import itertools
import unittest

from src.NLP.NLPAlgo.classical_pipeline import ClassicalNLPPipeline, PipelineStageError
from src.NLP.NLPAlgo.common.logic_types import InvalidLogicalExpressionError, LogicalExpression, LogicalOp
from src.NLP.NLPAlgo.entity_extraction.distance_matcher import EntityResolver
from src.NLP.NLPAlgo.logical_to_cnf import CNFCompilationError, LogicalToCNFCompiler
from src.NLP.NLPAlgo.syntactic_parsing.dependency_parser import LogicalDependencyParser
from src.NLP.NLPAlgo.tokenization_normalization.lemmatizer import LogicLemmatizer
from src.NLP.NLPAlgo.tokenization_normalization.math_tokenizer import MathTokenizer


def parse_expression(text: str) -> LogicalExpression:
    tokenizer = MathTokenizer()
    normalizer = LogicLemmatizer()
    rules, math_map = tokenizer.split_rules(text)
    if len(rules) != 1:
        raise AssertionError(f"Expected one expression, got {rules!r}")
    normalized = normalizer.expand_contractions(rules[0])
    tokens = tokenizer.tokenize_expression(normalized)
    parsed = LogicalDependencyParser().parse(tokens, normalized)
    return ClassicalNLPPipeline._restore_math_atoms(parsed, math_map)


def eval_expression(expression: LogicalExpression, values: dict[str, bool]) -> bool:
    if expression.op == LogicalOp.FACT:
        return values[EntityResolver.canonical_key(expression.proposition or "")]
    children = [eval_expression(child, values) for child in expression.operands]
    if expression.op == LogicalOp.NOT:
        return not children[0]
    if expression.op == LogicalOp.AND:
        return all(children)
    if expression.op == LogicalOp.OR:
        return any(children)
    if expression.op == LogicalOp.IMPLIES:
        return not children[0] or children[1]
    if expression.op == LogicalOp.EQUIV:
        return children[0] == children[1]
    raise AssertionError(f"Unexpected operator: {expression.op}")


def cnf_satisfiable_with_original_assignment(clauses, original_ids, assignment) -> bool:
    all_ids = {abs(lit) for clause in clauses for lit in clause if lit}
    aux_ids = sorted(all_ids - set(original_ids.values()))
    for aux_values in itertools.product((False, True), repeat=len(aux_ids)):
        values = {original_ids[atom]: value for atom, value in assignment.items()}
        values.update(dict(zip(aux_ids, aux_values)))
        if all(any(values[abs(lit)] == (lit > 0) for lit in clause if lit) for clause in clauses):
            return True
    return False


class ParserTests(unittest.TestCase):
    def test_precedence_and_parentheses(self):
        expression = parse_expression("A AND B OR C")
        self.assertEqual(expression.op, LogicalOp.OR)
        self.assertEqual(expression.operands[0].op, LogicalOp.AND)

        grouped = parse_expression("A AND (B OR C)")
        self.assertEqual(grouped.op, LogicalOp.AND)
        self.assertEqual(grouped.operands[1].op, LogicalOp.OR)

    def test_negation_prefix_and_copular_form(self):
        prefix = parse_expression("NOT (A OR B)")
        self.assertEqual(prefix.op, LogicalOp.NOT)
        self.assertEqual(prefix.operands[0].op, LogicalOp.OR)

        natural = parse_expression("The engine is not running")
        self.assertEqual(natural.op, LogicalOp.NOT)
        self.assertEqual(natural.operands[0].proposition, "The engine is running")

    def test_if_then_and_equivalence(self):
        implication = parse_expression("If A, B AND C")
        self.assertEqual(implication.op, LogicalOp.IMPLIES)
        self.assertEqual(implication.operands[1].op, LogicalOp.AND)

        equivalence = parse_expression("A if and only if B")
        self.assertEqual(equivalence.op, LogicalOp.EQUIV)

    def test_quoted_atoms_can_contain_operator_words(self):
        expression = parse_expression('"A AND B" OR C')
        self.assertEqual(expression.operands[0].proposition, "A AND B")

    def test_malformed_and_unsupported_input_is_rejected(self):
        for text in ("A AND", "IF A B", "(A OR B", "A XOR B", "A unless B", "A eventually B"):
            with self.subTest(text=text):
                with self.assertRaises((PipelineStageError, ValueError)):
                    ClassicalNLPPipeline().compile_text(text)

    def test_multiple_rules_share_exact_proposition_identity(self):
        request = ClassicalNLPPipeline().compile_text(
            "The engine is running. NOT (the engine is running)."
        )
        self.assertEqual(request.metadata.variable_mapping, {"1": "the engine is running"})
        self.assertIn([1, 0], request.clauses)
        self.assertIn([-1, 0], request.clauses)

    def test_repeated_compilation_has_stable_variable_ids(self):
        first = ClassicalNLPPipeline().compile_text("B OR A")
        second = ClassicalNLPPipeline().compile_text("A OR B")
        self.assertEqual(first.metadata.variable_mapping, second.metadata.variable_mapping)

    def test_empty_input_has_distinct_error(self):
        with self.assertRaisesRegex(ValueError, "cannot be empty"):
            ClassicalNLPPipeline().compile_text("  \n")


class ILRTests(unittest.TestCase):
    def test_invalid_tree_shapes_are_rejected(self):
        invalid = [
            LogicalExpression(LogicalOp.NOT),
            LogicalExpression(LogicalOp.AND),
            LogicalExpression(LogicalOp.IMPLIES, (LogicalExpression(LogicalOp.FACT, proposition="A"),)),
            LogicalExpression(LogicalOp.FACT),
        ]
        for node in invalid:
            with self.subTest(node=node):
                with self.assertRaises(InvalidLogicalExpressionError):
                    node.validate()


class CNFTruthTableTests(unittest.TestCase):
    EXPRESSIONS = (
        "NOT A",
        "A AND B",
        "A OR B",
        "A IMPLIES B",
        "A IFF B",
        "NOT (A OR B)",
        "(A AND B) OR C",
        "A IFF (B OR C)",
    )

    def test_tseitin_cnf_matches_truth_tables(self):
        for text in self.EXPRESSIONS:
            with self.subTest(text=text):
                expression = parse_expression(text)
                atom_ids, mapping = EntityResolver().resolve_many(expression.atoms())
                result = LogicalToCNFCompiler().compile([expression], atom_ids, mapping)
                atoms = sorted(atom_ids)
                for bits in itertools.product((False, True), repeat=len(atoms)):
                    original_values = dict(zip(atoms, bits))
                    expected = eval_expression(expression, original_values)
                    actual = cnf_satisfiable_with_original_assignment(result.clauses, atom_ids, original_values)
                    self.assertEqual(actual, expected, (text, original_values))

    def test_compiler_rejects_missing_resolved_atoms(self):
        expression = parse_expression("A")
        with self.assertRaises(CNFCompilationError):
            LogicalToCNFCompiler().compile([expression], {}, {})

    def test_math_relation_is_preserved_as_an_atom(self):
        request = ClassicalNLPPipeline().compile_text("x + y <= 10")
        self.assertIn("x + y <= 10", request.metadata.variable_mapping.values())


if __name__ == "__main__":
    unittest.main()
