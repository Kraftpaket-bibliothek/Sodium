"""End-to-end relational problem solving test using Dynamic Domain Matrix and CDCL solver.

Encodes and solves Exercise 31.1 from Max-Planck-Institut / Saarland University
Tutorials for Automated Reasoning (WS26/27).
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Dict, List

from src.NLP.cnf_contract import SodiumCNFRequest
from src.NLP.NLPAlgo.classical_pipeline import ClassicalNLPPipeline
from src.NLP.NLPAlgo.entity_extraction.domain_matrix import DynamicDomainMatrix
from src.NLP.NLPAlgo.logical_to_cnf import LogicalToCNFCompiler
from src.NLP.solver_bridge import SodiumExecutionBridge


class TestRelationalNLPSolver(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("No C compiler is available for the real solver integration test.")

        cls.repo_root = Path(__file__).resolve().parents[3]
        cls.tempdir = tempfile.TemporaryDirectory(prefix="sodium_nlp_relational_")
        if sys.platform == "darwin":
            library = Path(cls.tempdir.name) / "libsodium_sat.dylib"
            shared_flag = "-dynamiclib"
        else:
            library = Path(cls.tempdir.name) / "libsodium_sat.so"
            shared_flag = "-shared"
        sources = [
            "src/SAT/sat_api.c",
            "src/SAT/CDCL/CDCL.c",
            "src/SAT/CDCL/Clauses/clause_array.c",
            "src/SAT/CDCL/VSIDS.c",
            "src/SAT/CDCL/memory.c",
        ]
        command = [
            compiler,
            "-std=c11",
            "-O1",
            "-fPIC",
            shared_flag,
            "-Isrc/SAT",
            "-Isrc/SAT/CDCL",
            *sources,
            "-lm",
            "-o",
            str(library),
        ]
        subprocess.run(command, cwd=cls.repo_root, check=True, capture_output=True, text=True)
        cls.bridge = SodiumExecutionBridge(lib_path=str(library))
        if not cls.bridge.is_available():
            raise unittest.SkipTest("The compiled Sodium shared library could not be loaded.")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "tempdir"):
            cls.tempdir.cleanup()

    def test_exercise_31_solving_and_rule_verification(self) -> None:
        """Solves Exercise 31.1 from Saarland WS26/27 via Dynamic Domain Matrix."""
        columns = [f"House {i}" for i in range(1, 6)]
        categories = {
            "Color": ["Red", "Yellow", "Blue", "White", "Green"],
            "Nationality": ["British", "German", "Danish", "Norwegian", "Swedish"],
            "Pet": ["Dog", "Horse", "Cat", "Budgerigar", "Fish"],
            "Food": ["Pizza", "Pretzels", "Strawberries", "Sushi", "Waffles"],
            "Drink": ["Milk", "Beer", "Tea", "Coffee", "Water"],
        }
        matrix = DynamicDomainMatrix(columns=columns, categories=categories)

        # Build relational clues from the exercise specification
        additional_clauses: List[List[int]] = []

        # 1. The British man lives in the second house, which is not blue.
        additional_clauses.extend(matrix.clause_at_position("British", 2))
        additional_clauses.extend(matrix.clause_not_at_position("Blue", 2))

        # 2. The dog lives in the yellow house.
        additional_clauses.extend(matrix.clause_co_located("Dog", "Yellow"))

        # 3. The green house is next to the blue house. The blue house is not at either end.
        additional_clauses.extend(matrix.clause_adjacent("Green", "Blue"))
        additional_clauses.extend(matrix.clause_not_at_position("Blue", 1))
        additional_clauses.extend(matrix.clause_not_at_position("Blue", 5))

        # 4. The man who eats sushi lives next to the man who drinks beer.
        additional_clauses.extend(matrix.clause_adjacent("Sushi", "Beer"))

        # 5. The German man lives in the red house and eats pretzels.
        additional_clauses.extend(matrix.clause_co_located("German", "Red"))
        additional_clauses.extend(matrix.clause_co_located("German", "Pretzels"))

        # 6. The horse owner drinks milk and does not eat sushi.
        additional_clauses.extend(matrix.clause_co_located("Horse", "Milk"))
        additional_clauses.extend(matrix.clause_not_co_located("Horse", "Sushi"))

        # 7. No two Scandinavians live next to each other.
        # (Scandinavians: Danish, Norwegian, Swedish)
        additional_clauses.extend(matrix.clause_group_mutual_exclusion(["Danish", "Norwegian", "Swedish"]))

        # 8. The Danish man eats strawberries and does not live next to the man who drinks water.
        additional_clauses.extend(matrix.clause_co_located("Danish", "Strawberries"))
        additional_clauses.extend(matrix.clause_not_adjacent("Danish", "Water"))

        # 9. The cat lives in the middle house.
        additional_clauses.extend(matrix.clause_at_position("Cat", 3))

        # 10. The coffee drinker eats waffles.
        additional_clauses.extend(matrix.clause_co_located("Coffee", "Waffles"))

        # 11. The budgerigar owner drinks tea and lives at one end of the row.
        additional_clauses.extend(matrix.clause_co_located("Budgerigar", "Tea"))
        # At one end means House 1 or House 5
        additional_clauses.append([matrix.var_id("Budgerigar", 1), matrix.var_id("Budgerigar", 5), 0])

        # 12. The Norwegian man does not own a cat.
        additional_clauses.extend(matrix.clause_not_co_located("Norwegian", "Cat"))

        # 13. The pet in the white house is not a mammal. (Mammals: Dog, Horse, Cat)
        additional_clauses.extend(matrix.clause_not_co_located("White", "Dog"))
        additional_clauses.extend(matrix.clause_not_co_located("White", "Horse"))
        additional_clauses.extend(matrix.clause_not_co_located("White", "Cat"))

        # Compile via LogicalToCNFCompiler with matrix
        compiler = LogicalToCNFCompiler()
        compiled = compiler.compile(
            expressions=[],
            atom_ids={},
            variable_mapping=matrix.get_variable_mapping(),
            matrix=matrix,
            additional_clauses=additional_clauses,
        )

        request = SodiumCNFRequest(
            metadata={
                "description": "Max-Planck-Institut Saarland Exercise 31.1",
                "variable_mapping": compiled.variable_mapping,
            },
            clauses=compiled.clauses,
            assumptions=compiled.assumptions,
        )

        # Solve via C CDCL bridge
        pipeline = ClassicalNLPPipeline(solver_bridge=self.bridge)
        response = pipeline.solve(request)
        solver_resp = response["response"]
        self.assertEqual(solver_resp.get("status"), "SATISFIABLE")
        self.assertEqual(solver_resp.get("status_code"), 10)

        # Extract truth assignment from raw_model
        raw_model = solver_resp.get("raw_model", [])
        true_vars = {lit for lit in raw_model if lit > 0}

        # Decode house assignment for each property
        house: Dict[str, int] = {}
        for r, item_name in enumerate(matrix.row_names):
            for c in range(1, matrix.num_cols + 1):
                vid = matrix.var_id_by_row(r, c)
                if vid in true_vars:
                    house[item_name] = c

        # Verify all 25 items have been assigned to a house (1..5)
        self.assertEqual(len(house), 25)
        for item_name in matrix.row_names:
            self.assertIn(item_name, house)
            self.assertTrue(1 <= house[item_name] <= 5)

        # 1. The British man lives in the second house, which is not blue.
        self.assertEqual(house["British"], 2)
        self.assertNotEqual(house["Blue"], 2)

        # 2. The dog lives in the yellow house.
        self.assertEqual(house["Dog"], house["Yellow"])

        # 3. The green house is next to the blue house. The blue house is not at either end.
        self.assertEqual(abs(house["Green"] - house["Blue"]), 1)
        self.assertNotIn(house["Blue"], [1, 5])

        # 4. The man who eats sushi lives next to the man who drinks beer.
        self.assertEqual(abs(house["Sushi"] - house["Beer"]), 1)

        # 5. The German man lives in the red house and eats pretzels.
        self.assertEqual(house["German"], house["Red"])
        self.assertEqual(house["German"], house["Pretzels"])

        # 6. The horse owner drinks milk and does not eat sushi.
        self.assertEqual(house["Horse"], house["Milk"])
        self.assertNotEqual(house["Horse"], house["Sushi"])

        # 7. No two Scandinavians live next to each other.
        self.assertGreater(abs(house["Danish"] - house["Norwegian"]), 1)
        self.assertGreater(abs(house["Danish"] - house["Swedish"]), 1)
        self.assertGreater(abs(house["Norwegian"] - house["Swedish"]), 1)

        # 8. The Danish man eats strawberries and does not live next to the man who drinks water.
        self.assertEqual(house["Danish"], house["Strawberries"])
        self.assertGreater(abs(house["Danish"] - house["Water"]), 1)

        # 9. The cat lives in the middle house.
        self.assertEqual(house["Cat"], 3)

        # 10. The coffee drinker eats waffles.
        self.assertEqual(house["Coffee"], house["Waffles"])

        # 11. The budgerigar owner drinks tea and lives at one end of the row.
        self.assertEqual(house["Budgerigar"], house["Tea"])
        self.assertIn(house["Budgerigar"], [1, 5])

        # 12. The Norwegian man does not own a cat.
        self.assertNotEqual(house["Norwegian"], house["Cat"])

        # 13. The pet in the white house is not a mammal. (Mammals: Dog, Horse, Cat)
        self.assertNotEqual(house["White"], house["Dog"])
        self.assertNotEqual(house["White"], house["Horse"])
        self.assertNotEqual(house["White"], house["Cat"])

    def test_contradictory_clue_proves_unsat(self) -> None:
        """Adding a contradictory clue (British in House 1 while clue 1 says House 2) yields UNSAT."""
        columns = [f"House {i}" for i in range(1, 6)]
        categories = {
            "Nationality": ["British", "German"],
        }
        matrix = DynamicDomainMatrix(columns=columns, categories=categories)
        cl1 = matrix.clause_at_position("British", 2)
        cl2 = matrix.clause_at_position("British", 1)  # Directly contradicts cl1

        compiler = LogicalToCNFCompiler()
        compiled = compiler.compile([], {}, matrix.get_variable_mapping(), matrix=matrix, additional_clauses=cl1 + cl2)

        request = SodiumCNFRequest(
            metadata={"description": "Contradictory Houses", "variable_mapping": compiled.variable_mapping},
            clauses=compiled.clauses,
        )

        pipeline = ClassicalNLPPipeline(solver_bridge=self.bridge)
        response = pipeline.solve(request)
        self.assertEqual(response["response"].get("status"), "UNSATISFIABLE")
        self.assertEqual(response["response"].get("status_code"), 20)


if __name__ == "__main__":
    unittest.main()
