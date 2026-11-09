from __future__ import annotations
import argparse
import ctypes
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Literal, Optional, Union

# ----------------------------------------------------------------------------
# Pydantic Import & Cross-Version Compatibility Layer
# ----------------------------------------------------------------------------
try:
    from pydantic import (
        BaseModel,
        Field,
        field_validator,
        model_validator,
        ValidationError,
    )
    PYDANTIC_V2 = True
except ImportError:
    try:
        from pydantic import (  # type: ignore
            BaseModel,
            Field,
            validator as field_validator,
            root_validator as model_validator,
            ValidationError,
        )
        PYDANTIC_V2 = False
    except ImportError:
        sys.stderr.write(
            "ERROR: 'pydantic' is not installed.\n"
            "Please install Pydantic using:\n"
            "    pip install pydantic\n"
        )
        sys.exit(1)


# ============================================================================
# SECTION 1: SYSTEM PROMPT (Synchronized with src/NLP/nlp_api.c)
# ============================================================================

SODIUM_NLP_SYSTEM_PROMPT = """You are Sodium's Neuro-Symbolic Logic Translator. Your task is to rigorously extract
all logical entities, constraints, dependencies, and assumptions from the user's
natural language specification and output a single, strictly valid JSON document
in Conjunctive Normal Form (CNF) ready for processing by Sodium's CDCL engine.

TARGET JSON FORMAT (OUTPUT ONLY THIS JSON SCHEMA):
{
  "format": "cnf",
  "solver_config": {
    "name": "sodium-cdcl",
    "timeout_seconds": 10,
    "verbose": false
  },
  "metadata": {
    "description": "<Brief summary of logical domain>",
    "variable_mapping": {
      "1": "<DescriptiveVariableName1>",
      "2": "<DescriptiveVariableName2>"
    }
  },
  "clauses": [
    [ 1, 2, 0 ],
    [ -1, 3, 0 ]
  ],
  "assumptions": [
    1
  ]
}

TRANSLATION RULES:
1. Every distinct atomic proposition must be assigned a unique positive integer starting from 1.
2. In 'clauses':
   - Positive integer N represents proposition N is TRUE.
   - Negative integer -N represents proposition N is FALSE.
   - Every clause array MUST end with 0 as a terminator (e.g. [-1, 2, 0]).
3. Standard Logic Equivalences:
   - 'IF A THEN B' becomes (NOT A OR B) -> [-A, B, 0]
   - 'A and B cannot be active together' becomes (NOT A OR NOT B) -> [-A, -B, 0]
   - 'Choose exactly ONE of A or B' becomes [[A, B, 0], [-A, -B, 0]]
   - 'Always require A' becomes [A, 0]
   - 'Require A unless B' becomes (A OR B) -> [A, B, 0]
4. Put any specific hypothetical condition or what-if query in the 'assumptions' array.
"""


# ============================================================================
# SECTION 2: PYDANTIC SCHEMA DEFINITIONS
# ============================================================================

class SolverConfig(BaseModel):
    name: str = Field(
        default="sodium-cdcl",
        description="Target solver identifier, defaults to 'sodium-cdcl'"
    )
    timeout_seconds: int = Field(
        default=10,
        ge=1,
        description="Maximum execution timeout in seconds"
    )
    verbose: bool = Field(
        default=False,
        description="Enable verbose diagnostics during search"
    )


class Metadata(BaseModel):
    description: str = Field(
        ...,
        description="Summary of the logical domain and requirements being solved"
    )
    variable_mapping: Dict[str, str] = Field(
        ...,
        description="Dictionary mapping stringified positive integer IDs ('1', '2', ...) to human-readable variable names"
    )

    if PYDANTIC_V2:
        @field_validator("variable_mapping")
        @classmethod
        def validate_variable_keys(cls, v: Dict[str, str]) -> Dict[str, str]:
            for k in v.keys():
                if not k.isdigit() or int(k) <= 0:
                    raise ValueError(f"Variable mapping key '{k}' must be a positive integer string (e.g. '1', '2')")
            return v
    else:
        @field_validator("variable_mapping")  # type: ignore
        def validate_variable_keys(cls, v: Dict[str, str]) -> Dict[str, str]:
            for k in v.keys():
                if not str(k).isdigit() or int(k) <= 0:
                    raise ValueError(f"Variable mapping key '{k}' must be a positive integer string (e.g. '1', '2')")
            return v


class SodiumCNFRequest(BaseModel):
    format: Literal["cnf"] = Field(
        default="cnf",
        description="Data representation format; must be 'cnf'"
    )
    solver_config: SolverConfig = Field(
        default_factory=SolverConfig,
        description="Configuration parameters for the CDCL solver"
    )
    metadata: Metadata = Field(
        ...,
        description="Metadata and mapping of integer IDs to descriptive variable names"
    )
    clauses: List[List[int]] = Field(
        ...,
        description="List of CNF clauses. Each clause contains signed non-zero integer literals, terminating with 0."
    )
    assumptions: List[int] = Field(
        default_factory=list,
        description="Optional list of signed non-zero assumption literals to enforce during solving"
    )

    if PYDANTIC_V2:
        @field_validator("clauses")
        @classmethod
        def validate_and_normalize_clauses(cls, clauses: List[List[int]]) -> List[List[int]]:
            if not clauses:
                raise ValueError("Clauses array cannot be empty.")

            normalized: List[List[int]] = []
            for idx, clause in enumerate(clauses):
                if not clause:
                    raise ValueError(f"Clause at index {idx} cannot be empty.")

                c = list(clause)
                # Auto-append terminal 0 if omitted by LLM
                if c[-1] != 0:
                    c.append(0)

                literals = c[:-1]
                if not literals:
                    raise ValueError(f"Clause at index {idx} contains only a 0 terminator with no literals.")

                for lit in literals:
                    if lit == 0:
                        raise ValueError(f"Clause at index {idx} contains premature 0 literal before the terminator.")

                normalized.append(c)
            return normalized

        @field_validator("assumptions")
        @classmethod
        def validate_assumptions(cls, assumptions: List[int]) -> List[int]:
            for lit in assumptions:
                if lit == 0:
                    raise ValueError("Assumption literals must be non-zero signed integers.")
            return assumptions

        @model_validator(mode="after")
        def verify_variable_consistency(self) -> "SodiumCNFRequest":
            vmap = self.metadata.variable_mapping
            for clause in self.clauses:
                for lit in clause[:-1]:
                    var_id = abs(lit)
                    key = str(var_id)
                    if key not in vmap:
                        vmap[key] = f"AutoVar_{var_id}"
            return self

    else:
        @field_validator("clauses")  # type: ignore
        def validate_and_normalize_clauses_v1(cls, clauses: List[List[int]]) -> List[List[int]]:
            if not clauses:
                raise ValueError("Clauses array cannot be empty.")
            normalized: List[List[int]] = []
            for idx, clause in enumerate(clauses):
                if not clause:
                    raise ValueError(f"Clause at index {idx} cannot be empty.")
                c = list(clause)
                if c[-1] != 0:
                    c.append(0)
                literals = c[:-1]
                if not literals:
                    raise ValueError(f"Clause at index {idx} contains only a 0 terminator with no literals.")
                for lit in literals:
                    if lit == 0:
                        raise ValueError(f"Clause at index {idx} contains premature 0 literal before terminator.")
                normalized.append(c)
            return normalized

        @field_validator("assumptions")  # type: ignore
        def validate_assumptions_v1(cls, assumptions: List[int]) -> List[int]:
            for lit in assumptions:
                if lit == 0:
                    raise ValueError("Assumption literals must be non-zero signed integers.")
            return assumptions

        @model_validator  # type: ignore
        def verify_variable_consistency_v1(cls, values: Dict[str, Any]) -> Dict[str, Any]:
            metadata = values.get("metadata")
            clauses = values.get("clauses")
            if metadata and clauses and hasattr(metadata, "variable_mapping"):
                vmap = metadata.variable_mapping
                for clause in clauses:
                    for lit in clause[:-1]:
                        var_id = abs(lit)
                        key = str(var_id)
                        if key not in vmap:
                            vmap[key] = f"AutoVar_{var_id}"
            return values

    @classmethod
    def get_json_schema(cls) -> Dict[str, Any]:
        """Returns the JSON schema dict across Pydantic v1 and v2."""
        if PYDANTIC_V2:
            return cls.model_json_schema()
        else:
            return cls.schema()  # type: ignore

    @classmethod
    def from_json_string(cls, json_str: str) -> "SodiumCNFRequest":
        """Deserializes and strictly validates a JSON string."""
        if PYDANTIC_V2:
            return cls.model_validate_json(json_str)
        else:
            return cls.parse_raw(json_str)  # type: ignore

    def to_json_string(self, indent: int = 2) -> str:
        """Serializes model to formatted JSON string."""
        if PYDANTIC_V2:
            return self.model_dump_json(indent=indent)
        else:
            return self.json(indent=indent)  # type: ignore


@dataclass
class LLMConfig:
    """Operational settings for the LLM inference provider."""
    provider: str = "local"  # 'local', 'ollama', 'openai', 'vllm', or 'custom'
    model: str = "llama3"
    base_url: str = "http://localhost:11434"
    api_key: str = ""
    timeout: float = 60.0
    temperature: float = 0.0

    @classmethod
    def from_env(cls) -> "LLMConfig":
        """Loads configuration overrides from standard environment variables."""
        provider = os.getenv("SODIUM_LLM_PROVIDER", "local").lower()
        default_base = "http://localhost:11434" if provider in ("local", "ollama") else "https://api.openai.com/v1"
        default_model = "llama3" if provider in ("local", "ollama") else "gpt-4o"

        return cls(
            provider=provider,
            model=os.getenv("SODIUM_LLM_MODEL", default_model),
            base_url=os.getenv("SODIUM_LLM_BASE_URL", default_base).rstrip("/"),
            api_key=os.getenv("OPENAI_API_KEY", os.getenv("SODIUM_LLM_API_KEY", "")),
            timeout=float(os.getenv("SODIUM_LLM_TIMEOUT", "60.0")),
            temperature=float(os.getenv("SODIUM_LLM_TEMPERATURE", "0.0")),
        )


class LLMClientGateway:

    def __init__(self, config: LLMConfig):
        self.config = config

    def generate_cnf_json(self, user_prompt: str) -> str:
        """
        Submits prompt to LLM with Pydantic JSON schema constraints.
        Returns the raw response text from the model.
        """
        provider = self.config.provider.lower()
        if provider in ("local", "ollama"):
            return self._call_ollama(user_prompt)
        elif provider in ("openai", "vllm", "custom"):
            return self._call_openai_compatible(user_prompt)
        else:
            # Default to OpenAI compatible format
            return self._call_openai_compatible(user_prompt)

    def _call_ollama(self, user_prompt: str) -> str:
        """
        Calls Ollama API. Supports:
        1. Native /api/chat with GBNF grammar via 'format': <json_schema>
        2. Fallback to /v1/chat/completions if preferred.
        """
        base = self.config.base_url.rstrip("/")
        schema = SodiumCNFRequest.get_json_schema()

        # Check if user specified a /v1 endpoint explicitly
        if base.endswith("/v1"):
            return self._call_openai_compatible(user_prompt)

        endpoint = f"{base}/api/chat"
        payload = {
            "model": self.config.model,
            "messages": [
                {"role": "system", "content": SODIUM_NLP_SYSTEM_PROMPT},
                {"role": "user", "content": user_prompt}
            ],
            "stream": False,
            "options": {
                "temperature": self.config.temperature
            },
            # Ollama passes JSON schema directly to llama.cpp's GBNF grammar engine:
            "format": schema
        }

        resp_dict = self._http_post_json(endpoint, payload)
        message = resp_dict.get("message", {})
        content = message.get("content", "")
        if not content:
            raise RuntimeError(f"Ollama returned empty response message: {resp_dict}")
        return content

    def _call_openai_compatible(self, user_prompt: str) -> str:
        base = self.config.base_url.rstrip("/")
        endpoint = f"{base}/chat/completions" if base.endswith("/v1") else f"{base}/v1/chat/completions"
        schema = SodiumCNFRequest.get_json_schema()

        # OpenAI Structured Outputs contract
        payload: Dict[str, Any] = {
            "model": self.config.model,
            "messages": [
                {"role": "system", "content": SODIUM_NLP_SYSTEM_PROMPT},
                {"role": "user", "content": user_prompt}
            ],
            "temperature": self.config.temperature,
            "response_format": {
                "type": "json_schema",
                "json_schema": {
                    "name": "SodiumCNFRequest",
                    "strict": True,
                    "schema": schema
                }
            }
        }

        headers: Dict[str, str] = {}
        if self.config.api_key:
            headers["Authorization"] = f"Bearer {self.config.api_key}"

        resp_dict = self._http_post_json(endpoint, payload, headers=headers)
        choices = resp_dict.get("choices", [])
        if not choices:
            raise RuntimeError(f"API returned no choices: {resp_dict}")

        content = choices[0].get("message", {}).get("content", "")
        if not content:
            raise RuntimeError(f"API choice contains empty content: {choices[0]}")
        return content

    def _http_post_json(
        self,
        url: str,
        payload: Dict[str, Any],
        headers: Optional[Dict[str, str]] = None
    ) -> Dict[str, Any]:
        """Executes HTTP POST using Python's standard library urllib."""
        data_bytes = json.dumps(payload).encode("utf-8")
        req_headers = {
            "Content-Type": "application/json",
            "Accept": "application/json",
            "User-Agent": "Sodium-NLP-Router/1.0"
        }
        if headers:
            req_headers.update(headers)

        req = urllib.request.Request(url, data=data_bytes, headers=req_headers, method="POST")

        try:
            with urllib.request.urlopen(req, timeout=self.config.timeout) as response:
                body = response.read().decode("utf-8")
                return json.loads(body)
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP Error {e.code} from {url}: {err_body}") from e
        except urllib.error.URLError as e:
            raise ConnectionError(
                f"Failed to connect to LLM endpoint at {url}: {e.reason}.\n"
                f"Ensure the server (e.g. Ollama or local API) is running."
            ) from e


# ============================================================================
# SECTION 4: SANITIZER & PARSER
# ============================================================================

def strip_markdown_fences(text: str) -> str:
    """
    Extracts raw JSON content from markdown blocks or surrounding conversational text.
    Handles:
      ```json
      { ... }
      ```
    or raw '{ ... }' blocks.
    """
    text = text.strip()

    # Pattern 1: Fenced code block ```json ... ``` or ``` ... ```
    fence_pattern = r"^```(?:json)?\s*\n?(.*?)\n?```$"
    match = re.search(fence_pattern, text, re.DOTALL | re.IGNORECASE)
    if match:
        return match.group(1).strip()

    # Pattern 2: Scan for innermost or top-level JSON object '{ ... }'
    start = text.find("{")
    end = text.rfind("}")
    if start != -1 and end != -1 and end > start:
        return text[start:end + 1].strip()

    return text


# ============================================================================
# SECTION 5: C CDCL SOLVER BRIDGE (ctypes)
# ============================================================================

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
        self.lib = self._load_library(lib_path)
        self._init_function_signatures()

    def _load_library(self, lib_path: Optional[str]) -> Optional[ctypes.CDLL]:
        candidates: List[Path] = []
        if lib_path:
            candidates.append(Path(lib_path))

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
                except Exception:
                    continue
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
            sys.stderr.write(f"Warning: Symbol missing in Sodium shared library: {e}\n")
            self.lib = None

    def solve_json(self, cnf_json_str: str) -> Dict[str, Any]:
        """Invokes sodium_api_solve_json in C and returns the parsed result."""
        if not self.lib:
            raise RuntimeError(
                "Sodium C library not loaded. Compile libsodium_sat.(dylib/so) to enable direct solving."
            )

        out_ptr = ctypes.c_char_p()
        input_bytes = cnf_json_str.encode("utf-8")

        status_code = self.lib.sodium_api_solve_json(input_bytes, ctypes.byref(out_ptr))

        response_str = "{}"
        if out_ptr.value:
            response_str = out_ptr.value.decode("utf-8")
            self.lib.sodium_api_free_string(out_ptr)

        try:
            parsed = json.loads(response_str)
            parsed["status_code"] = status_code
            return parsed
        except Exception:
            return {
                "status_code": status_code,
                "raw_response": response_str
            }


# ============================================================================
# SECTION 6: HIGH-LEVEL ROUTER API
# ============================================================================

class SodiumNLPRouter:

    def __init__(self, config: Optional[LLMConfig] = None):
        self.config = config or LLMConfig.from_env()
        self.gateway = LLMClientGateway(self.config)
        self.bridge = SodiumExecutionBridge()

    def translate(self, natural_language_text: str) -> SodiumCNFRequest:
        """
        Translates natural language text into a strictly validated SodiumCNFRequest.
        Raises ValidationError if the output violates schema constraints.
        """
        raw_text = self.gateway.generate_cnf_json(natural_language_text)
        clean_json_str = strip_markdown_fences(raw_text)

        try:
            return SodiumCNFRequest.from_json_string(clean_json_str)
        except ValidationError as e:
            sys.stderr.write(f"\n[Validation Error] LLM output violated Sodium CNF Schema:\n{e}\n")
            sys.stderr.write(f"Raw LLM Output:\n{raw_text}\n")
            raise

    def solve(self, request: Union[SodiumCNFRequest, str]) -> Dict[str, Any]:
        """Solves a CNF request via the Sodium C CDCL engine."""
        if isinstance(request, SodiumCNFRequest):
            json_str = request.to_json_string()
        else:
            json_str = request

        return self.bridge.solve_json(json_str)


# ============================================================================
# SECTION 7: COMMAND LINE INTERFACE (CLI)
# ============================================================================

def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sodium NLP Router: Translates natural language requirements into CDCL CNF formulas with Pydantic validation."
    )
    input_group = parser.add_mutually_exclusive_group()
    input_group.add_argument(
        "-t", "--text",
        type=str,
        help="Input natural language prompt directly from command line."
    )
    input_group.add_argument(
        "-f", "--file",
        type=str,
        help="Path to a text file containing natural language specifications."
    )
    input_group.add_argument(
        "--dump-schema",
        action="store_true",
        help="Print Sodium's Pydantic CNF JSON Schema to stdout and exit."
    )

    parser.add_argument(
        "-e", "--engine",
        type=str,
        default="llm",
        choices=["llm", "non-llm"],
        help="NLP translation engine: 'llm' (neural API/local model) or 'non-llm' (deterministic classical NLP algorithm)."
    )
    parser.add_argument(
        "-p", "--provider",
        type=str,
        default="local",
        choices=["local", "ollama", "openai", "vllm", "custom"],
        help="LLM inference provider (default: local)."
    )
    parser.add_argument(
        "-m", "--model",
        type=str,
        default=None,
        help="Model identifier (e.g. llama3, mistral, gpt-4o, gpt-4o-mini)."
    )
    parser.add_argument(
        "-u", "--base-url",
        type=str,
        default=None,
        help="Base URL for the inference endpoint (default: http://localhost:11434)."
    )
    parser.add_argument(
        "-k", "--api-key",
        type=str,
        default=None,
        help="API Key for authenticated endpoints (or set OPENAI_API_KEY environment variable)."
    )
    parser.add_argument(
        "-o", "--output",
        type=str,
        default=None,
        help="Optional file path to write validated CNF JSON output."
    )
    parser.add_argument(
        "-s", "--solve",
        action="store_true",
        help="Directly invoke Sodium's C CDCL solver on the validated CNF model."
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print verbose diagnostics and raw API exchanges."
    )

    return parser.parse_args()


def main() -> None:
    args = parse_arguments()

    if args.dump_schema:
        print(json.dumps(SodiumCNFRequest.get_json_schema(), indent=2))
        return

    # Determine input text
    input_text: str = ""
    if args.text:
        input_text = args.text
    elif args.file:
        file_path = Path(args.file)
        if not file_path.is_file():
            sys.stderr.write(f"Error: Specified file not found: {args.file}\n")
            sys.exit(1)
        input_text = file_path.read_text(encoding="utf-8")
    else:
        # Check if stdin has data piped
        if not sys.stdin.isatty():
            input_text = sys.stdin.read()
        else:
            sys.stderr.write("Error: Please provide input via --text, --file, or piped stdin.\n")
            sys.stderr.write("Run with --help for available options.\n")
            sys.exit(1)

    if not input_text.strip():
        sys.stderr.write("Error: Input text is empty.\n")
        sys.exit(1)

    # Configure client
    config = LLMConfig.from_env()
    if args.provider:
        config.provider = args.provider
    if args.model:
        config.model = args.model
    if args.base_url:
        config.base_url = args.base_url
    if args.api_key:
        config.api_key = args.api_key

    if args.engine == "non-llm":
        if args.verbose:
            sys.stderr.write("[*] Using Classical Non-LLM NLP Pipeline (Deterministic Algorithm)\n")
        try:
            from .NLPAlgo.classical_pipeline import ClassicalNLPPipeline
        except ImportError:
            from NLPAlgo.classical_pipeline import ClassicalNLPPipeline
        pipeline = ClassicalNLPPipeline()
        try:
            cnf_request = pipeline.compile_text(input_text)
        except Exception as e:
            sys.stderr.write(f"\n[Classical NLP Compilation Failed]: {e}\n")
            sys.exit(1)
    else:
        router = SodiumNLPRouter(config)

        if args.verbose:
            sys.stderr.write(f"[*] Provider: {config.provider}\n")
            sys.stderr.write(f"[*] Base URL: {config.base_url}\n")
            sys.stderr.write(f"[*] Model:    {config.model}\n")
            sys.stderr.write("[*] Sending structured request with Pydantic JSON schema...\n")

        try:
            cnf_request = router.translate(input_text)
        except Exception as e:
            sys.stderr.write(f"\n[Translation Failed]: {e}\n")
            sys.exit(1)

    formatted_json = cnf_request.to_json_string(indent=2)

    # Output JSON
    if args.output:
        out_path = Path(args.output)
        out_path.write_text(formatted_json, encoding="utf-8")
        print(f"Validated CNF JSON written to: {out_path}")
    else:
        print(formatted_json)

    # Optional solve step
    if args.solve:
        if not router.bridge.is_available():
            sys.stderr.write(
                "Error: Sodium C shared library (libsodium_sat.dylib/.so) not found.\n"
                "To build the shared library, run:\n"
                "    clang -shared -fPIC -O3 -Isrc/SAT src/SAT/sat_api.c src/SAT/CDCL/CDCL.c \\\n"
                "          src/SAT/CDCL/Clauses/clause_array.c src/SAT/CDCL/VSIDS.c \\\n"
                "          src/SAT/CDCL/Memory/memory.c -o libsodium_sat.dylib\n"
            )
            sys.exit(2)

        result = router.solve(cnf_request)
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
