from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from src.NLP.NLPAlgo.classical_pipeline import ClassicalNLPPipeline
from src.NLP.solver_bridge import SodiumExecutionBridge


class SolverIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("No C compiler is available for the real solver integration test.")

        cls.repo_root = Path(__file__).resolve().parents[3]
        cls.tempdir = tempfile.TemporaryDirectory(prefix="sodium_nlp_solver_")
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

    def test_natural_language_to_sat_and_assignment(self):
        pipeline = ClassicalNLPPipeline(solver_bridge=self.bridge)
        request = pipeline.compile_text("The engine is running AND the battery is connected.")
        response = pipeline.solve(request)
        solver_response = response["response"]
        self.assertEqual(solver_response.get("status"), "SATISFIABLE")
        self.assertEqual(solver_response.get("status_code"), 10)

    def test_contradictory_rules_reach_unsat(self):
        pipeline = ClassicalNLPPipeline(solver_bridge=self.bridge)
        request = pipeline.compile_text("The engine is running. The engine is not running.")
        response = pipeline.solve(request)
        solver_response = response["response"]
        self.assertEqual(solver_response.get("status"), "UNSATISFIABLE")
        self.assertEqual(solver_response.get("status_code"), 20)


if __name__ == "__main__":
    unittest.main()
