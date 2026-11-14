from __future__ import annotations

import ctypes
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional


class SolverLibraryError(RuntimeError):
    """Raised when the Sodium solver shared library cannot be loaded."""


class SolverExecutionError(RuntimeError):
    """Raised when the C API call or its response is invalid."""


class SodiumExecutionBridge:
    """
    Direct in-memory bridge to Sodium's C CDCL solver (sat_api.c)
    via ctypes calling sodium_api_solve_json().
    """

    LIB_NAMES = [
        "libsodium_sat.dylib",
        "libsodium_sat.so",
        "libsodium_sat.dll",
    ]

    def __init__(self, lib_path: Optional[str] = None):
        self.load_error: Optional[str] = None
        self.lib = self._load_library(lib_path)
        self._init_function_signatures()

    def _load_library(self, lib_path: Optional[str]) -> Optional[ctypes.CDLL]:
        candidates: List[Path] = []
        if lib_path:
            explicit_path = Path(lib_path)
            if not explicit_path.is_file():
                self.load_error = f"Explicit solver library path does not exist: {explicit_path}"
                return None
            candidates.append(explicit_path)

        # Search relative to repo root
        current_dir = Path(__file__).resolve().parent
        repo_root = current_dir.parent.parent

        search_dirs = [
            repo_root,
            repo_root / "build",
            repo_root / "lib",
            current_dir.parent / "SAT",
        ]

        for s_dir in search_dirs:
            for name in self.LIB_NAMES:
                candidates.append(s_dir / name)

        for p in candidates:
            if p.is_file():
                try:
                    return ctypes.CDLL(str(p))
                except OSError as exc:
                    self.load_error = f"Could not load solver library {p}: {exc}"
                    continue
        if self.load_error is None:
            self.load_error = "No Sodium solver shared library was found in the supported search paths."
        return None

    def is_available(self) -> bool:
        return self.lib is not None

    def _init_function_signatures(self) -> None:
        if not self.lib:
            return

        try:
            # int sodium_api_solve_json(const char* json_request, char** out_json_response);
            self.lib.sodium_api_solve_json.argtypes = [
                ctypes.c_char_p,
                ctypes.POINTER(ctypes.c_char_p),
            ]
            self.lib.sodium_api_solve_json.restype = ctypes.c_int

            # void sodium_api_free_string(char* str);
            self.lib.sodium_api_free_string.argtypes = [ctypes.c_char_p]
            self.lib.sodium_api_free_string.restype = None
        except AttributeError as e:
            self.load_error = f"Required Sodium C API symbol is missing: {e}"
            sys.stderr.write(f"Warning: {self.load_error}\n")
            self.lib = None

    def solve_json(self, cnf_json_str: str) -> Dict[str, Any]:
        """Invokes sodium_api_solve_json in C and returns the parsed result."""
        if not isinstance(cnf_json_str, str) or not cnf_json_str.strip():
            raise ValueError("Solver request must be a non-empty JSON string.")
        if not self.lib:
            raise SolverLibraryError(self.load_error or "Sodium C solver library is unavailable.")

        out_ptr = ctypes.c_char_p()
        response_bytes: Optional[bytes] = None
        try:
            input_bytes = cnf_json_str.encode("utf-8")
            status_code = self.lib.sodium_api_solve_json(input_bytes, ctypes.byref(out_ptr))
            response_bytes = out_ptr.value
        except Exception as exc:
            raise SolverExecutionError(f"C solver API call failed: {exc}") from exc
        finally:
            # The C API allocates this buffer; release it even if decoding/parsing fails.
            if out_ptr.value:
                self.lib.sodium_api_free_string(out_ptr)

        if response_bytes is None:
            raise SolverExecutionError(f"C solver API returned no response (status {status_code}).")

        try:
            response_str = response_bytes.decode("utf-8")
            parsed = json.loads(response_str)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise SolverExecutionError(f"C solver returned an invalid JSON response: {exc}") from exc
        if not isinstance(parsed, dict):
            raise SolverExecutionError("C solver response must be a JSON object.")
        if status_code not in (0, 10, 20):
            raise SolverExecutionError(f"C solver returned unsupported status code {status_code}.")
        try:
            parsed["status_code"] = status_code
            return parsed
        except Exception as exc:
            raise SolverExecutionError(f"Could not normalize C solver response: {exc}") from exc
