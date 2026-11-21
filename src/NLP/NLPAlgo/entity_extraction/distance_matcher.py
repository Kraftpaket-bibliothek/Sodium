"""Deterministic proposition identity resolution for the logical compiler."""

from __future__ import annotations

import re
from typing import Dict, Iterable, Optional, Tuple

from .domain_matrix import DynamicDomainMatrix


class EntityResolutionError(ValueError):
    """Raised when a proposition cannot be assigned a valid canonical identity."""


class EntityResolver:
    """Assigns exact, stable IDs; supported by DynamicDomainMatrix for multi-dimensional grounding."""

    def __init__(self, matrix: Optional[DynamicDomainMatrix] = None):
        self.matrix = matrix if matrix is not None else DynamicDomainMatrix(columns=["State"])
        self.canonical_entities: Dict[str, int] = {}
        self.var_names: Dict[int, str] = {}

    @staticmethod
    def canonical_key(entity_name: str) -> str:
        if not isinstance(entity_name, str):
            raise EntityResolutionError("Proposition names must be strings.")
        name = " ".join(entity_name.split())
        if not name:
            raise EntityResolutionError("Proposition names cannot be empty.")
        return name.casefold()

    def _parse_coordinate(self, name: str) -> Optional[Tuple[str, int]]:
        clean = " ".join(name.strip().split())
        for prop_key, row_idx in self.matrix.row_by_key.items():
            prop_name = self.matrix.row_names[row_idx]
            pattern = rf"^{re.escape(prop_name)}(?:\s+(?:is\s+in|in|at)\s+|[_\s]+)(.+)$"
            m = re.match(pattern, clean, re.IGNORECASE)
            if m:
                col_part = m.group(1).strip().replace("_", " ")
                for col_idx, col_name in enumerate(self.matrix.columns, start=1):
                    if (
                        col_part.casefold() == col_name.casefold()
                        or col_part == str(col_idx)
                        or col_part.casefold().endswith(f" {col_idx}")
                        or col_part.casefold() == f"module {col_idx}"
                        or col_part.casefold() == f"house {col_idx}"
                    ):
                        return prop_name, col_idx
        return None

    def resolve_many(self, entity_names: Iterable[str]) -> Tuple[Dict[str, int], Dict[str, str]]:
        """Assigns IDs in canonical lexical order, independent of rule order."""
        names_by_key: Dict[str, str] = {}
        raw_by_key: Dict[str, str] = {}
        for name in entity_names:
            key = self.canonical_key(name)
            names_by_key.setdefault(key, key)
            raw_by_key.setdefault(key, name)

        # If a multi-dimensional matrix is active, use coordinate allocation
        if self.matrix.num_cols > 1 or self.matrix.num_rows > 0:
            self.canonical_entities = {}
            for key in sorted(names_by_key):
                raw_name = raw_by_key[key]
                coord = self._parse_coordinate(raw_name)
                if coord is not None:
                    prop_name, col_idx = coord
                    row_idx = self.matrix.register_property(prop_name)
                    self.canonical_entities[key] = self.matrix.var_id_by_row(row_idx, col_idx)
                else:
                    row_idx = self.matrix.register_property(names_by_key[key])
                    self.canonical_entities[key] = self.matrix.var_id_by_row(row_idx, 1)
            return dict(self.canonical_entities), self.matrix.get_variable_mapping()

        self.canonical_entities = {}
        self.var_names = {}
        for var_id, key in enumerate(sorted(names_by_key), start=1):
            self.canonical_entities[key] = var_id
            self.var_names[var_id] = names_by_key[key]
        return dict(self.canonical_entities), self.get_variable_mapping()

    def get_or_register_variable(self, entity_name: str) -> int:
        key = self.canonical_key(entity_name)
        if self.matrix.num_cols > 1:
            row_idx = self.matrix.register_property(entity_name)
            return self.matrix.var_id_by_row(row_idx, 1)

        if key not in self.canonical_entities:
            var_id = len(self.canonical_entities) + 1
            self.canonical_entities[key] = var_id
            self.var_names[var_id] = key
        return self.canonical_entities[key]

    def get_variable_mapping(self) -> Dict[str, str]:
        if self.matrix.num_cols > 1 or self.matrix.num_rows > 0:
            return self.matrix.get_variable_mapping()
        return {str(var_id): name for var_id, name in self.var_names.items()}


# Compatibility for callers that imported the former class name. Resolution is
# exact and deterministic.
EntityDistanceMatcher = EntityResolver
