"""Unit tests for the Dynamic Domain Matrix Layer in Sodium NLP."""

from __future__ import annotations

import unittest

from src.NLP.NLPAlgo.entity_extraction.domain_matrix import (
    DomainAxisDetector,
    DynamicDomainMatrix,
    MatrixAllocationError,
)


class TestDomainAxisDetector(unittest.TestCase):
    def test_detect_five_houses(self) -> None:
        text = "Five houses, each a different colour, stand in a row."
        cols = DomainAxisDetector.detect_columns(text)
        self.assertEqual(cols, ["House 1", "House 2", "House 3", "House 4", "House 5"])

    def test_detect_explicit_slots(self) -> None:
        text = "There are 4 slots in the rack."
        cols = DomainAxisDetector.detect_columns(text)
        self.assertEqual(cols, ["Slot 1", "Slot 2", "Slot 3", "Slot 4"])

    def test_detect_ordinals(self) -> None:
        text = "The first item and second item must not overlap in the middle."
        cols = DomainAxisDetector.detect_columns(text)
        self.assertIsNotNone(cols)
        self.assertTrue(len(cols) >= 2)

    def test_flat_logic_returns_none(self) -> None:
        text = "PrimaryServer AND (NOT StandbyServer OR BackupActive)"
        cols = DomainAxisDetector.detect_columns(text)
        self.assertIsNone(cols)


class TestDynamicDomainMatrix(unittest.TestCase):
    def setUp(self) -> None:
        self.columns = [f"House {i}" for i in range(1, 6)]
        self.categories = {
            "Color": ["Red", "Yellow", "Blue", "White", "Green"],
            "Nationality": ["British", "German", "Danish", "Norwegian", "Swedish"],
            "Pet": ["Dog", "Horse", "Cat", "Budgerigar", "Fish"],
            "Food": ["Pizza", "Pretzels", "Strawberries", "Sushi", "Waffles"],
            "Drink": ["Milk", "Beer", "Tea", "Coffee", "Water"],
        }
        self.matrix = DynamicDomainMatrix(columns=self.columns, categories=self.categories)

    def test_dimensions(self) -> None:
        self.assertEqual(self.matrix.num_cols, 5)
        self.assertEqual(self.matrix.num_rows, 25)
        self.assertEqual(self.matrix.total_variables, 125)

    def test_exercise_31_variable_mapping_exact_ids(self) -> None:
        """Verify variable IDs match Saarland Exercise 31.1 Page 2 table."""
        # Row 0: Red -> 1..5
        self.assertEqual(self.matrix.var_id("Red", 1), 1)
        self.assertEqual(self.matrix.var_id("Red", 2), 2)
        self.assertEqual(self.matrix.var_id("Red", 3), 3)
        self.assertEqual(self.matrix.var_id("Red", 4), 4)
        self.assertEqual(self.matrix.var_id("Red", 5), 5)

        # Row 1: Yellow -> 6..10
        self.assertEqual(self.matrix.var_id("Yellow", 1), 6)
        self.assertEqual(self.matrix.var_id("Yellow", 5), 10)

        # Row 2: Blue -> 11..15
        self.assertEqual(self.matrix.var_id("Blue", 1), 11)
        self.assertEqual(self.matrix.var_id("Blue", 5), 15)

        # Row 3: White -> 16..20
        self.assertEqual(self.matrix.var_id("White", 1), 16)
        self.assertEqual(self.matrix.var_id("White", 5), 20)

        # Row 4: Green -> 21..25
        self.assertEqual(self.matrix.var_id("Green", 1), 21)
        self.assertEqual(self.matrix.var_id("Green", 5), 25)

        # Row 5: British -> 26..30
        self.assertEqual(self.matrix.var_id("British", 1), 26)
        self.assertEqual(self.matrix.var_id("British", 2), 27)
        self.assertEqual(self.matrix.var_id("British", 5), 30)

        # Row 6: German -> 31..35
        self.assertEqual(self.matrix.var_id("German", 1), 31)
        self.assertEqual(self.matrix.var_id("German", 5), 35)

        # Row 10: Dog -> 51..55
        self.assertEqual(self.matrix.var_id("Dog", 1), 51)
        self.assertEqual(self.matrix.var_id("Dog", 5), 55)

        # Row 12: Cat -> 61..65 (Cat in middle house = House 3 -> 63)
        self.assertEqual(self.matrix.var_id("Cat", 3), 63)

        # Row 20: Milk -> 101..105
        self.assertEqual(self.matrix.var_id("Milk", 1), 101)
        self.assertEqual(self.matrix.var_id("Milk", 5), 105)

        # Row 24: Water -> 121..125
        self.assertEqual(self.matrix.var_id("Water", 1), 121)
        self.assertEqual(self.matrix.var_id("Water", 5), 125)

    def test_variable_metadata_mapping(self) -> None:
        mapping = self.matrix.get_variable_mapping()
        self.assertEqual(len(mapping), 125)
        self.assertEqual(mapping["1"], "Red_House_1")
        self.assertEqual(mapping["27"], "British_House_2")
        self.assertEqual(mapping["63"], "Cat_House_3")
        self.assertEqual(mapping["125"], "Water_House_5")

    def test_structural_bijective_clauses(self) -> None:
        """Verify Exact-One (ALO + AMO) clauses across rows and columns."""
        clauses = self.matrix.generate_structural_clauses()
        # Row bijectivity: 25 rows * (1 ALO + 10 AMO) = 275 clauses
        # Column bijectivity: 5 categories * 5 cols * (1 ALO + 10 AMO) = 275 clauses
        # Total = 550 clauses
        self.assertEqual(len(clauses), 550)

        # Check all clauses are 0-terminated
        for cl in clauses:
            self.assertEqual(cl[-1], 0)

        # Check Red ALO clause: [1, 2, 3, 4, 5, 0]
        self.assertIn([1, 2, 3, 4, 5, 0], clauses)
        # Check Red AMO: [-1, -2, 0]
        self.assertIn([-1, -2, 0], clauses)

    def test_relational_clause_helpers(self) -> None:
        # 1. At position: British in House 2
        cl_at = self.matrix.clause_at_position("British", 2)
        self.assertEqual(cl_at, [[27, 0]])

        # 2. Not at position: Blue not in House 2
        cl_not_at = self.matrix.clause_not_at_position("Blue", 2)
        self.assertEqual(cl_not_at, [[-12, 0]])

        # 3. Co-located: Dog in Yellow house
        cl_co = self.matrix.clause_co_located("Dog", "Yellow")
        # 5 cols * 2 directions = 10 clauses
        self.assertEqual(len(cl_co), 10)
        # Dog(51) <-> Yellow(6)
        self.assertIn([-51, 6, 0], cl_co)
        self.assertIn([-6, 51, 0], cl_co)

        # 4. Adjacent: Green next to Blue
        cl_adj = self.matrix.clause_adjacent("Green", "Blue")
        # Col 1: Green(21) -> Blue(12)
        self.assertIn([-21, 12, 0], cl_adj)
        # Col 5: Green(25) -> Blue(14)
        self.assertIn([-25, 14, 0], cl_adj)
        # Col 3: Green(23) -> Blue(12) or Blue(14)
        self.assertIn([-23, 12, 14, 0], cl_adj)

        # 5. Not adjacent: Danish not next to Water
        cl_not_adj = self.matrix.clause_not_adjacent("Danish", "Water")
        # 4 pairs * 2 directions = 8 clauses
        self.assertEqual(len(cl_not_adj), 8)
        # Danish(36) and Water(122) cannot both be true
        self.assertIn([-36, -122, 0], cl_not_adj)

    def test_bounds_and_error_handling(self) -> None:
        with self.assertRaises(MatrixAllocationError):
            self.matrix.var_id("Red", 0)
        with self.assertRaises(MatrixAllocationError):
            self.matrix.var_id("Red", 6)
        with self.assertRaises(MatrixAllocationError):
            self.matrix.var_id("NonExistent", 1)

    def test_1d_flat_propositional_compatibility(self) -> None:
        matrix_1d = DynamicDomainMatrix(columns=["State"])
        r1 = matrix_1d.register_property("Alpha")
        r2 = matrix_1d.register_property("Beta")
        self.assertEqual(matrix_1d.num_cols, 1)
        self.assertEqual(matrix_1d.var_id("Alpha", 1), 1)
        self.assertEqual(matrix_1d.var_id("Beta", 1), 2)
        # 1D should emit zero structural clauses
        self.assertEqual(matrix_1d.generate_structural_clauses(), [])
        # Mapping should use bare names
        mapping = matrix_1d.get_variable_mapping()
        self.assertEqual(mapping["1"], "Alpha")
        self.assertEqual(mapping["2"], "Beta")


if __name__ == "__main__":
    unittest.main()
