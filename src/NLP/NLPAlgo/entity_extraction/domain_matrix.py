"""Dynamic Domain Matrix Layer for coordinate-based variable allocation in Sodium.

Provides multi-dimensional (Row x Column) variable indexing matching standard
industrial and academic SAT formulations (e.g. Einstein's riddle / Max-Planck-Institut
Saarland Exercise 31.1), while smoothly collapsing to 1D (C=1) for flat propositional logic.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Set, Tuple


class MatrixAllocationError(ValueError):
    """Raised when domain matrix coordinates or dimensions are invalid."""


# Standard ordinal and positional words mapping to 1-based column indices
ORDINALS_MAP = {
    "first": 1,
    "1st": 1,
    "second": 2,
    "2nd": 2,
    "third": 3,
    "3rd": 3,
    "fourth": 4,
    "4th": 4,
    "fifth": 5,
    "5th": 5,
    "sixth": 6,
    "6th": 6,
    "seventh": 7,
    "7th": 7,
    "eighth": 8,
    "8th": 8,
    "ninth": 9,
    "9th": 9,
    "tenth": 10,
    "10th": 10,
}


@dataclass
class DomainAxisDetector:
    """Detects domain column axis (containers/slots) vs property row axis from input text."""

    @classmethod
    def detect_columns(cls, text: str) -> Optional[List[str]]:
        """Scans for spatial/ordered sequences (e.g., 'five houses', ordinals).

        Returns list of column names if an ordered axis is detected, else None (indicating 1D).
        """
        lower = text.lower()

        # Check for explicit number of houses/slots/days/racks
        m = re.search(r"\b(five|5)\s+(houses|slots|positions|racks|steps)\b", lower)
        if m:
            container_type = m.group(2).rstrip("s").capitalize()
            return [f"{container_type} {i}" for i in range(1, 6)]

        m_num = re.search(r"\b(\d+)\s+(houses|slots|positions|racks|steps|days)\b", lower)
        if m_num:
            n = int(m_num.group(1))
            container_type = m_num.group(2).rstrip("s").capitalize()
            if 1 < n <= 100:
                return [f"{container_type} {i}" for i in range(1, n + 1)]

        # Check for ordinals indicating row sequence
        ordinals_found = set()
        for word, idx in ORDINALS_MAP.items():
            if re.search(rf"\b{re.escape(word)}\b", lower):
                ordinals_found.add(idx)

        if len(ordinals_found) >= 2 or (ordinals_found and "row" in lower):
            max_idx = max(max(ordinals_found), 5 if "middle" in lower else 2)
            return [f"Position {i}" for i in range(1, max_idx + 1)]

        return None


class DynamicDomainMatrix:
    """N x M Variable Coordinate Matrix for DIMACS CDCL Grounding.

    Rows: Properties / Attributes (e.g. Red, Yellow, British, German, Dog, Milk...)
    Columns: Domain Coordinates / Slots (e.g. House 1, House 2, ... House 5)

    Formula (1-based DIMACS variable ID):
        VarID(row, col) = row * |Columns| + col  (where row in [0, R-1], col in [1, C])
    """

    def __init__(
        self,
        columns: Optional[Sequence[str]] = None,
        categories: Optional[Mapping[str, Sequence[str]]] = None,
    ):
        self.columns: List[str] = list(columns) if columns else ["State"]
        self.num_cols: int = len(self.columns)
        if self.num_cols <= 0:
            raise MatrixAllocationError("Matrix must have at least one column.")

        self.categories: Dict[str, List[str]] = {}
        self.row_names: List[str] = []
        self.row_by_key: Dict[str, int] = {}
        self.category_of_row: Dict[int, str] = {}

        if categories:
            for cat_name, items in categories.items():
                self.register_category(cat_name, items)

    @staticmethod
    def canonical_key(name: str) -> str:
        clean = " ".join(name.strip().split())
        return clean.casefold()

    def register_category(self, category_name: str, items: Iterable[str]) -> None:
        """Registers a group of mutually exclusive properties belonging to one category."""
        clean_cat = category_name.strip()
        if clean_cat not in self.categories:
            self.categories[clean_cat] = []

        for item in items:
            key = self.canonical_key(item)
            if key not in self.row_by_key:
                row_idx = len(self.row_names)
                self.row_names.append(item.strip())
                self.row_by_key[key] = row_idx
                self.category_of_row[row_idx] = clean_cat
                self.categories[clean_cat].append(key)

    def register_property(self, item: str, category_name: str = "General") -> int:
        """Registers a single property in a category, returning its row index."""
        key = self.canonical_key(item)
        if key in self.row_by_key:
            return self.row_by_key[key]

        row_idx = len(self.row_names)
        self.row_names.append(item.strip())
        self.row_by_key[key] = row_idx
        self.category_of_row[row_idx] = category_name
        self.categories.setdefault(category_name, []).append(key)
        return row_idx

    @property
    def num_rows(self) -> int:
        return len(self.row_names)

    @property
    def total_variables(self) -> int:
        return self.num_rows * self.num_cols

    def get_row_index(self, item: str) -> int:
        key = self.canonical_key(item)
        if key not in self.row_by_key:
            raise MatrixAllocationError(f"Property {item!r} has not been registered in the matrix.")
        return self.row_by_key[key]

    def var_id(self, item: str, col: int) -> int:
        """Returns the DIMACS 1-based variable identifier for an entity at coordinate col (1-based)."""
        if col < 1 or col > self.num_cols:
            raise MatrixAllocationError(
                f"Column index {col} is out of bounds (valid range: 1..{self.num_cols})."
            )
        row = self.get_row_index(item)
        return row * self.num_cols + col

    def var_id_by_row(self, row: int, col: int) -> int:
        if row < 0 or row >= self.num_rows:
            raise MatrixAllocationError(f"Row index {row} out of bounds (0..{self.num_rows-1}).")
        if col < 1 or col > self.num_cols:
            raise MatrixAllocationError(f"Column index {col} out of bounds (1..{self.num_cols}).")
        return row * self.num_cols + col

    def get_variable_mapping(self) -> Dict[str, str]:
        """Returns { '1': 'Red_House1', ... } for the solver response metadata."""
        mapping: Dict[str, str] = {}
        for r, name in enumerate(self.row_names):
            clean_name = "_".join(name.split())
            for c in range(1, self.num_cols + 1):
                vid = r * self.num_cols + c
                if self.num_cols == 1:
                    mapping[str(vid)] = clean_name
                else:
                    col_name = "_".join(self.columns[c - 1].split())
                    mapping[str(vid)] = f"{clean_name}_{col_name}"
        return mapping

    def generate_structural_clauses(self) -> List[List[int]]:
        """Generates exact-one (ALO + AMO) structural axioms for the matrix grid."""
        clauses: List[List[int]] = []
        if self.num_cols <= 1:
            return clauses  # 1D mode has no spatial ALO/AMO grid constraints

        # 1. Row Bijectivity: Each attribute exists at exactly one column
        for r in range(self.num_rows):
            # At-least-one column
            alo = [self.var_id_by_row(r, c) for c in range(1, self.num_cols + 1)] + [0]
            clauses.append(alo)
            # At-most-one column
            for c1 in range(1, self.num_cols + 1):
                for c2 in range(c1 + 1, self.num_cols + 1):
                    clauses.append([-self.var_id_by_row(r, c1), -self.var_id_by_row(r, c2), 0])

        # 2. Column Bijectivity: In each category, each column contains exactly one attribute
        for cat_name, item_keys in self.categories.items():
            if len(item_keys) <= 1:
                continue
            row_indices = [self.row_by_key[k] for k in item_keys]
            for c in range(1, self.num_cols + 1):
                # At-least-one attribute in column
                alo = [self.var_id_by_row(r, c) for r in row_indices] + [0]
                clauses.append(alo)
                # At-most-one attribute in column
                for i1 in range(len(row_indices)):
                    for i2 in range(i1 + 1, len(row_indices)):
                        clauses.append([
                            -self.var_id_by_row(row_indices[i1], c),
                            -self.var_id_by_row(row_indices[i2], c),
                            0,
                        ])

        return clauses

    # -------------------------------------------------------------------------
    # Relational Constraint Generators
    # -------------------------------------------------------------------------

    def clause_at_position(self, item: str, col: int) -> List[List[int]]:
        """Item is at column col."""
        return [[self.var_id(item, col), 0]]

    def clause_not_at_position(self, item: str, col: int) -> List[List[int]]:
        """Item is not at column col."""
        return [[-self.var_id(item, col), 0]]

    def clause_co_located(self, item1: str, item2: str) -> List[List[int]]:
        """item1 and item2 occupy the same column."""
        clauses: List[List[int]] = []
        for c in range(1, self.num_cols + 1):
            v1 = self.var_id(item1, c)
            v2 = self.var_id(item2, c)
            clauses.append([-v1, v2, 0])
            clauses.append([-v2, v1, 0])
        return clauses

    def clause_not_co_located(self, item1: str, item2: str) -> List[List[int]]:
        """item1 and item2 cannot occupy the same column."""
        clauses: List[List[int]] = []
        for c in range(1, self.num_cols + 1):
            v1 = self.var_id(item1, c)
            v2 = self.var_id(item2, c)
            clauses.append([-v1, -v2, 0])
        return clauses

    def clause_adjacent(self, item1: str, item2: str) -> List[List[int]]:
        """|col(item1) - col(item2)| == 1."""
        clauses: List[List[int]] = []
        if self.num_cols <= 1:
            return clauses

        # item1 implies item2 in adjacent columns
        clauses.append([-self.var_id(item1, 1), self.var_id(item2, 2), 0])
        clauses.append([-self.var_id(item1, self.num_cols), self.var_id(item2, self.num_cols - 1), 0])
        for c in range(2, self.num_cols):
            clauses.append([
                -self.var_id(item1, c),
                self.var_id(item2, c - 1),
                self.var_id(item2, c + 1),
                0,
            ])

        # Symmetric: item2 implies item1 in adjacent columns
        clauses.append([-self.var_id(item2, 1), self.var_id(item1, 2), 0])
        clauses.append([-self.var_id(item2, self.num_cols), self.var_id(item1, self.num_cols - 1), 0])
        for c in range(2, self.num_cols):
            clauses.append([
                -self.var_id(item2, c),
                self.var_id(item1, c - 1),
                self.var_id(item1, c + 1),
                0,
            ])

        return clauses

    def clause_not_adjacent(self, item1: str, item2: str) -> List[List[int]]:
        """item1 and item2 cannot be in adjacent columns."""
        clauses: List[List[int]] = []
        for c in range(1, self.num_cols):
            # Cannot have item1 at c and item2 at c+1
            clauses.append([-self.var_id(item1, c), -self.var_id(item2, c + 1), 0])
            # Cannot have item2 at c and item1 at c+1
            clauses.append([-self.var_id(item2, c), -self.var_id(item1, c + 1), 0])
        return clauses

    def clause_group_mutual_exclusion(self, group_items: Sequence[str]) -> List[List[int]]:
        """No two members of group can live next to each other."""
        clauses: List[List[int]] = []
        items = list(group_items)
        for i in range(len(items)):
            for j in range(i + 1, len(items)):
                clauses.extend(self.clause_not_adjacent(items[i], items[j]))
        return clauses
