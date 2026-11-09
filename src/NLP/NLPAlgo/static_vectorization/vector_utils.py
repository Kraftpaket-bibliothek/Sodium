# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/static_vectorization/vector_utils.py
# Description: Pure-Python linear algebra routines for dense static embeddings.
# ============================================================================

from __future__ import annotations
import math
from typing import List


class VectorUtils:
    """Zero-dependency vector operations (cosine similarity, normalization, centroids)."""

    @staticmethod
    def dot_product(v1: List[float], v2: List[float]) -> float:
        return sum(x * y for x, y in zip(v1, v2))

    @staticmethod
    def norm(v: List[float]) -> float:
        return math.sqrt(sum(x * x for x in v))

    @classmethod
    def normalize(cls, v: List[float]) -> List[float]:
        n = cls.norm(v)
        if n == 0.0:
            return [0.0] * len(v)
        return [x / n for x in v]

    @classmethod
    def cosine_similarity(cls, v1: List[float], v2: List[float]) -> float:
        n1 = cls.norm(v1)
        n2 = cls.norm(v2)
        if n1 == 0.0 or n2 == 0.0:
            return 0.0
        return cls.dot_product(v1, v2) / (n1 * n2)

    @staticmethod
    def add_scaled(target: List[float], source: List[float], scale: float = 1.0) -> List[float]:
        return [t + s * scale for t, s in zip(target, source)]
