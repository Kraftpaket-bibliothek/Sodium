from __future__ import annotations

import sys
from typing import Any, Dict, List, Literal

# ----------------------------------------------------------------------------
# Pydantic Import & Cross-Version Compatibility Layer
# ----------------------------------------------------------------------------
try:
    from pydantic import (
        BaseModel,
        Field,
        field_validator,
        model_validator,
    )
    PYDANTIC_V2 = True
except ImportError:
    try:
        from pydantic import (  # type: ignore
            BaseModel,
            Field,
            validator as field_validator,
            root_validator as model_validator,
        )
        PYDANTIC_V2 = False
    except ImportError:
        sys.stderr.write(
            "ERROR: 'pydantic' is not installed.\n"
            "Please install Pydantic using:\n"
            "    pip install pydantic\n"
        )
        sys.exit(1)


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
                # Normalize clauses by appending the DIMACS terminator when omitted.
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
