# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/entity_extraction/distance_matcher.py
# Description: Levenshtein distance and Jaro-Winkler string similarity
#              for entity deduplication and canonical variable resolution.
# ============================================================================

from __future__ import annotations
from typing import Dict, List, Tuple, Optional


class EntityDistanceMatcher:
    """
    Implements string distance metrics (Levenshtein and Jaro-Winkler)
    to deduplicate morphological entity variations and assign consistent
    proposition IDs.
    """

    def __init__(self, similarity_threshold: float = 0.82):
        self.threshold = similarity_threshold
        self.canonical_entities: Dict[str, int] = {}  # name -> var_id
        self.var_names: Dict[int, str] = {}           # var_id -> name
        self.next_var_id = 1

    @staticmethod
    def levenshtein_distance(s1: str, s2: str) -> int:
        """Computes the Levenshtein edit distance between two strings."""
        if s1 == s2:
            return 0
        if len(s1) == 0:
            return len(s2)
        if len(s2) == 0:
            return len(s1)

        v0 = list(range(len(s2) + 1))
        v1 = [0] * (len(s2) + 1)

        for i in range(len(s1)):
            v1[0] = i + 1
            for j in range(len(s2)):
                cost = 0 if s1[i] == s2[j] else 1
                v1[j + 1] = min(v1[j] + 1, v0[j + 1] + 1, v0[j] + cost)
            v0, v1 = v1, [0] * (len(s2) + 1)

        return v0[len(s2)]

    @classmethod
    def levenshtein_similarity(cls, s1: str, s2: str) -> float:
        """Returns normalized Levenshtein similarity between 0.0 and 1.0."""
        max_len = max(len(s1), len(s2))
        if max_len == 0:
            return 1.0
        dist = cls.levenshtein_distance(s1.lower(), s2.lower())
        return 1.0 - (dist / max_len)

    @staticmethod
    def jaro_winkler_similarity(s1: str, s2: str) -> float:
        """Computes Jaro-Winkler distance between two strings."""
        s1_low, s2_low = s1.lower(), s2.lower()
        if s1_low == s2_low:
            return 1.0

        len1, len2 = len(s1_low), len(s2_low)
        if len1 == 0 or len2 == 0:
            return 0.0

        match_bound = max(len1, len2) // 2 - 1
        s1_matches = [False] * len1
        s2_matches = [False] * len2

        matches = 0
        for i in range(len1):
            start = max(0, i - match_bound)
            end = min(i + match_bound + 1, len2)
            for j in range(start, end):
                if s2_matches[j]:
                    continue
                if s1_low[i] == s2_low[j]:
                    s1_matches[i] = True
                    s2_matches[j] = True
                    matches += 1
                    break

        if matches == 0:
            return 0.0

        # Count transpositions
        t = 0
        k = 0
        for i in range(len1):
            if not s1_matches[i]:
                continue
            while not s2_matches[k]:
                k += 1
            if s1_low[i] != s2_low[k]:
                t += 1
            k += 1
        transpositions = t / 2.0

        jaro = ((matches / len1) + (matches / len2) + ((matches - transpositions) / matches)) / 3.0

        # Winkler prefix bonus (up to 4 matching chars)
        prefix = 0
        for i in range(min(4, min(len1, len2))):
            if s1_low[i] == s2_low[i]:
                prefix += 1
            else:
                break

        return jaro + (prefix * 0.1 * (1.0 - jaro))

    def get_or_register_variable(self, entity_name: str) -> int:
        """
        Looks up or registers an entity name, returning its assigned integer variable ID.
        Fuzzy matches existing registered entities using Jaro-Winkler & Levenshtein.
        """
        raw = entity_name.strip()
        if not raw:
            raw = "UnnamedEntity"

        # Exact match
        if raw in self.canonical_entities:
            return self.canonical_entities[raw]

        # Case-insensitive match
        for existing_name, var_id in self.canonical_entities.items():
            if existing_name.lower() == raw.lower():
                self.canonical_entities[raw] = var_id
                return var_id

        # Fuzzy match against already registered entities
        best_match: Optional[str] = None
        best_score = 0.0

        for existing_name in self.canonical_entities.keys():
            # Never merge entities that differ in trailing designation suffixes (e.g. Server_A vs Server_B)
            tokens1 = raw.split('_')
            tokens2 = existing_name.split('_')
            if tokens1 and tokens2:
                last1, last2 = tokens1[-1], tokens2[-1]
                if (len(last1) <= 2 or last1.isdigit()) and (len(last2) <= 2 or last2.isdigit()):
                    if last1.lower() != last2.lower():
                        continue

            jw = self.jaro_winkler_similarity(raw, existing_name)
            lev = self.levenshtein_similarity(raw, existing_name)
            score = (jw + lev) / 2.0
            if score > best_score and score >= self.threshold and lev >= 0.70:
                best_score = score
                best_match = existing_name

        if best_match:
            var_id = self.canonical_entities[best_match]
            self.canonical_entities[raw] = var_id
            return var_id

        # New distinct variable
        new_id = self.next_var_id
        self.next_var_id += 1
        self.canonical_entities[raw] = new_id
        self.var_names[new_id] = raw
        return new_id

    def get_variable_mapping(self) -> Dict[str, str]:
        """Returns the dictionary mapping stringified variable IDs to canonical names."""
        return {str(var_id): name for var_id, name in self.var_names.items()}
