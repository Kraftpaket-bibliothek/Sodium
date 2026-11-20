"""Deterministic proposition identity resolution for the logical compiler."""

from __future__ import annotations

import re
from typing import Dict, Iterable, Tuple


class EntityResolutionError(ValueError):
    """Raised when a proposition cannot be assigned a valid canonical identity."""


class EntityResolver:
    """Assigns exact, stable IDs; it does not infer synonyms or fuzzy matches."""

    def __init__(self):
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

    def resolve_many(self, entity_names: Iterable[str]) -> Tuple[Dict[str, int], Dict[str, str]]:
        """Assigns IDs in canonical lexical order, independent of rule order."""
        names_by_key: Dict[str, str] = {}
        for name in entity_names:
            key = self.canonical_key(name)
            names_by_key.setdefault(key, key)

        self.canonical_entities = {}
        self.var_names = {}
        for var_id, key in enumerate(sorted(names_by_key), start=1):
            self.canonical_entities[key] = var_id
            self.var_names[var_id] = names_by_key[key]
        return dict(self.canonical_entities), self.get_variable_mapping()

    def get_or_register_variable(self, entity_name: str) -> int:
        key = self.canonical_key(entity_name)
        if key not in self.canonical_entities:
            var_id = len(self.canonical_entities) + 1
            self.canonical_entities[key] = var_id
            self.var_names[var_id] = key
        return self.canonical_entities[key]

    def get_variable_mapping(self) -> Dict[str, str]:
        return {str(var_id): name for var_id, name in self.var_names.items()}


# Compatibility for callers that imported the former class name. Resolution is
# now exact and deterministic; no string-distance matching is performed.
EntityDistanceMatcher = EntityResolver
