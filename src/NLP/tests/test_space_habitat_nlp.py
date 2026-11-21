"""Full end-to-end NLP pipeline integration test for the Space Habitat Puzzle.

Feeds controlled natural-language sentences into ClassicalNLPPipeline, validates
Tseitin compilation through DynamicDomainMatrix, and verifies solution via Sodium CDCL.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Dict

from src.NLP.NLPAlgo.classical_pipeline import ClassicalNLPPipeline
from src.NLP.NLPAlgo.entity_extraction.domain_matrix import DynamicDomainMatrix
from src.NLP.solver_bridge import SodiumExecutionBridge


class TestSpaceHabitatNLPPipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("No C compiler available.")

        cls.repo_root = Path(__file__).resolve().parents[3]
        cls.tempdir = tempfile.TemporaryDirectory(prefix="sodium_habitat_nlp_")
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
        cmd = [
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
        subprocess.run(cmd, cwd=cls.repo_root, check=True, capture_output=True, text=True)
        cls.bridge = SodiumExecutionBridge(lib_path=str(library))
        if not cls.bridge.is_available():
            raise unittest.SkipTest("Sodium library failed to load.")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "tempdir"):
            cls.tempdir.cleanup()

    def test_solve_via_structured_english_sentences(self):
        columns = [f"Module {i}" for i in range(1, 6)]
        categories = {
            "Colour": ["Crimson", "Amber", "Cobalt", "Silver", "Jade"],
            "Role": ["Doctor", "Mechanic", "Commander", "Lieutenant", "Ensign"],
            "Homeworld": ["Europa", "Titan", "Ganymede", "Mars", "Venus"],
            "Instrument": ["Scanner", "Wrench", "Beacon", "Drone", "Compass"],
            "Beverage": ["Espresso", "Cider", "Matcha", "Cocoa", "Electrolyte"],
        }
        matrix = DynamicDomainMatrix(columns=columns, categories=categories)

        # Structured natural-language sentences
        sentences = []

        # 1. Doctor in Module 2
        sentences.append("Doctor in Module 2.")

        # 2. Europa in Amber
        for m in range(1, 6):
            sentences.append(f"Europa in Module {m} if and only if Amber in Module {m}.")

        # 3. Jade immediately to right of Cobalt
        sentences.append("NOT (Cobalt in Module 5).")
        sentences.append("NOT (Jade in Module 1).")
        for m in range(1, 5):
            sentences.append(f"Cobalt in Module {m} if and only if Jade in Module {m+1}.")

        # 4. Drone next to Cider
        sentences.append("Drone in Module 1 implies Cider in Module 2.")
        sentences.append("Drone in Module 5 implies Cider in Module 4.")
        for m in range(2, 5):
            sentences.append(f"Drone in Module {m} implies (Cider in Module {m-1} OR Cider in Module {m+1}).")
        sentences.append("Cider in Module 1 implies Drone in Module 2.")
        sentences.append("Cider in Module 5 implies Drone in Module 4.")
        for m in range(2, 5):
            sentences.append(f"Cider in Module {m} implies (Drone in Module {m-1} OR Drone in Module {m+1}).")
        for m in range(1, 6):
            sentences.append(f"NOT (Drone in Module {m} AND Cider in Module {m}).")

        # 5. Mechanic in Crimson and operates Wrench
        for m in range(1, 6):
            sentences.append(f"Mechanic in Module {m} if and only if Crimson in Module {m}.")
            sentences.append(f"Mechanic in Module {m} if and only if Wrench in Module {m}.")

        # 6. Titan in Cobalt, drinks Espresso, not Drone
        for m in range(1, 6):
            sentences.append(f"Titan in Module {m} if and only if Cobalt in Module {m}.")
            sentences.append(f"Titan in Module {m} if and only if Espresso in Module {m}.")
            sentences.append(f"NOT (Titan in Module {m} AND Drone in Module {m}).")

        # 7. No two Command Officers live next to each other
        for m in range(1, 5):
            sentences.append(f"NOT (Commander in Module {m} AND Lieutenant in Module {m+1}).")
            sentences.append(f"NOT (Lieutenant in Module {m} AND Commander in Module {m+1}).")
            sentences.append(f"NOT (Commander in Module {m} AND Ensign in Module {m+1}).")
            sentences.append(f"NOT (Ensign in Module {m} AND Commander in Module {m+1}).")
            sentences.append(f"NOT (Lieutenant in Module {m} AND Ensign in Module {m+1}).")
            sentences.append(f"NOT (Ensign in Module {m} AND Lieutenant in Module {m+1}).")

        # 8. Commander operates Beacon, not next to Electrolyte
        for m in range(1, 6):
            sentences.append(f"Commander in Module {m} if and only if Beacon in Module {m}.")
            sentences.append(f"NOT (Commander in Module {m} AND Electrolyte in Module {m}).")
        for m in range(1, 5):
            sentences.append(f"NOT (Commander in Module {m} AND Electrolyte in Module {m+1}).")
            sentences.append(f"NOT (Electrolyte in Module {m} AND Commander in Module {m+1}).")

        # 9. Ganymede in middle module
        sentences.append("Ganymede in Module 3.")

        # 10. Cocoa operates Compass
        for m in range(1, 6):
            sentences.append(f"Cocoa in Module {m} if and only if Compass in Module {m}.")

        # 11. Mars drinks Matcha, at one end
        for m in range(1, 6):
            sentences.append(f"Mars in Module {m} if and only if Matcha in Module {m}.")
        sentences.append("Mars in Module 1 OR Mars in Module 5.")

        # 12. Lieutenant not from Ganymede
        for m in range(1, 6):
            sentences.append(f"NOT (Lieutenant in Module {m} AND Ganymede in Module {m}).")

        # 13. Silver in Module 5, not from moon
        sentences.append("Silver in Module 5.")
        for m in range(1, 6):
            sentences.append(f"NOT (Silver in Module {m} AND Europa in Module {m}).")
            sentences.append(f"NOT (Silver in Module {m} AND Titan in Module {m}).")
            sentences.append(f"NOT (Silver in Module {m} AND Ganymede in Module {m}).")

        # 14. Ensign not in Module 5
        sentences.append("NOT (Ensign in Module 5).")

        full_text = "\n".join(sentences)

        # Run completely through ClassicalNLPPipeline
        pipeline = ClassicalNLPPipeline(solver_bridge=self.bridge)
        request = pipeline.compile_text(full_text, matrix=matrix)

        response = pipeline.solve(request)
        solver_resp = response["response"]
        self.assertEqual(solver_resp.get("status"), "SATISFIABLE")
        self.assertEqual(solver_resp.get("status_code"), 10)

        # Decode truth assignment from raw_model
        raw_model = solver_resp.get("raw_model", [])
        true_vars = {lit for lit in raw_model if lit > 0}

        assignment: Dict[str, int] = {}
        for r, item_name in enumerate(matrix.row_names):
            for c in range(1, matrix.num_cols + 1):
                if matrix.var_id_by_row(r, c) in true_vars:
                    assignment[item_name] = c

        self.assertEqual(assignment["Amber"], 1)
        self.assertEqual(assignment["Cobalt"], 2)
        self.assertEqual(assignment["Jade"], 3)
        self.assertEqual(assignment["Crimson"], 4)
        self.assertEqual(assignment["Silver"], 5)

        self.assertEqual(assignment["Lieutenant"], 1)
        self.assertEqual(assignment["Doctor"], 2)
        self.assertEqual(assignment["Ensign"], 3)
        self.assertEqual(assignment["Mechanic"], 4)
        self.assertEqual(assignment["Commander"], 5)

        self.assertEqual(assignment["Europa"], 1)
        self.assertEqual(assignment["Titan"], 2)
        self.assertEqual(assignment["Ganymede"], 3)
        self.assertEqual(assignment["Venus"], 4)
        self.assertEqual(assignment["Mars"], 5)


if __name__ == "__main__":
    unittest.main()
