from __future__ import annotations

import ctypes
import unittest

from pydantic import ValidationError

from src.NLP.cnf_contract import Metadata, SodiumCNFRequest, SolverConfig
from src.NLP.solver_bridge import (
    SodiumExecutionBridge,
    SolverExecutionError,
    SolverLibraryError,
)


class ContractTests(unittest.TestCase):
    def test_default_solver_configuration_and_validation(self):
        config = SolverConfig()
        self.assertEqual(config.name, "sodium-cdcl")
        self.assertEqual(config.timeout_seconds, 10)
        self.assertFalse(config.verbose)
        with self.assertRaises(ValidationError):
            SolverConfig(timeout_seconds=0)

    def test_metadata_keys_are_positive_numeric_strings(self):
        Metadata(description="domain", variable_mapping={"1": "a"})
        for invalid in ({"0": "a"}, {"x": "a"}, {}):
            with self.subTest(invalid=invalid):
                if invalid:
                    with self.assertRaises(ValidationError):
                        Metadata(description="domain", variable_mapping=invalid)

    def test_clause_validation_and_terminators(self):
        request = SodiumCNFRequest(
            metadata={"description": "domain", "variable_mapping": {"1": "a"}},
            clauses=[[1], [-1, 0]],
        )
        self.assertEqual(request.clauses, [[1, 0], [-1, 0]])
        for clauses in ([], [[]], [[0]], [[1, 0, 2]]):
            with self.subTest(clauses=clauses):
                with self.assertRaises(ValidationError):
                    SodiumCNFRequest(
                        metadata={"description": "domain", "variable_mapping": {}},
                        clauses=clauses,
                    )

    def test_assumptions_mapping_and_json_round_trip(self):
        request = SodiumCNFRequest(
            metadata={"description": "domain", "variable_mapping": {"1": "named"}},
            clauses=[[1, -2]],
            assumptions=[3],
        )
        self.assertEqual(request.metadata.variable_mapping, {"1": "named", "2": "AutoVar_2"})
        with self.assertRaises(ValidationError):
            SodiumCNFRequest(
                metadata={"description": "domain", "variable_mapping": {}},
                clauses=[[1]],
                assumptions=[0],
            )
        serialized = request.to_json_string()
        restored = SodiumCNFRequest.from_json_string(serialized)
        self.assertEqual(restored.clauses, request.clauses)
        self.assertEqual(restored.assumptions, request.assumptions)
        self.assertEqual(restored.metadata.variable_mapping, request.metadata.variable_mapping)
        self.assertTrue(SodiumCNFRequest.get_json_schema())


class _FakeFunction:
    def __init__(self, callback):
        self.callback = callback
        self.argtypes = None
        self.restype = None

    def __call__(self, *args):
        return self.callback(*args)


class _FakeLibrary:
    def __init__(self, response: bytes, status: int = 10):
        self.response = response
        self.status = status
        self.buffer = None
        self.freed = False
        self.sodium_api_solve_json = _FakeFunction(self.solve)
        self.sodium_api_free_string = _FakeFunction(self.free)

    def solve(self, request, out_pointer):
        self.buffer = ctypes.create_string_buffer(self.response)
        pointer = ctypes.cast(out_pointer, ctypes.POINTER(ctypes.c_char_p))
        pointer[0] = ctypes.cast(self.buffer, ctypes.c_char_p)
        return self.status

    def free(self, pointer):
        self.freed = True


class BridgeTests(unittest.TestCase):
    def bridge_with_library(self, library):
        bridge = SodiumExecutionBridge.__new__(SodiumExecutionBridge)
        bridge.lib = library
        bridge.load_error = None
        bridge._init_function_signatures()
        return bridge

    def test_ctypes_signatures_and_json_response(self):
        library = _FakeLibrary(b'{"status":"SATISFIABLE"}', 10)
        bridge = self.bridge_with_library(library)
        result = bridge.solve_json('{"format":"cnf"}')
        self.assertEqual(result, {"status": "SATISFIABLE", "status_code": 10})
        self.assertTrue(library.freed)
        self.assertEqual(library.sodium_api_solve_json.restype, ctypes.c_int)
        self.assertEqual(library.sodium_api_free_string.restype, None)

    def test_invalid_json_is_reported_and_buffer_is_freed(self):
        library = _FakeLibrary(b"not-json")
        bridge = self.bridge_with_library(library)
        with self.assertRaises(SolverExecutionError):
            bridge.solve_json("{}")
        self.assertTrue(library.freed)

    def test_missing_shared_library_has_specific_error(self):
        bridge = SodiumExecutionBridge(lib_path="/no/such/libsodium_sat.so")
        self.assertFalse(bridge.is_available())
        with self.assertRaises(SolverLibraryError):
            bridge.solve_json("{}")


if __name__ == "__main__":
    unittest.main()
