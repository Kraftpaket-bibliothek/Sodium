# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/static_vectorization/static_embeddings.py
# Description: Zero-GPU static word vector lookup and deterministic feature hashing.
# ============================================================================

from __future__ import annotations
import math
import hashlib
from typing import List, Dict
from .vector_utils import VectorUtils


class StaticEmbeddingMatrix:
    """
    Lightweight, zero-GPU dense vector lookup.
    Provides fast 50-dimensional word representations for clustering and semantic alignment.
    """

    DIM = 50

    # Core semantic seed vectors for logic and system domains
    SEED_CONCEPTS = {
        "active": [0.8, 0.6, 0.1, -0.2, 0.5],
        "inactive": [-0.8, -0.6, -0.1, 0.2, -0.5],
        "require": [0.5, 0.9, 0.3, 0.4, 0.1],
        "forbid": [-0.7, 0.2, 0.8, -0.5, -0.3],
        "server": [0.3, 0.1, 0.7, 0.8, 0.2],
        "database": [0.2, 0.3, 0.8, 0.6, 0.4],
        "network": [0.1, 0.2, 0.6, 0.9, 0.5],
        "client": [0.4, 0.1, 0.5, 0.3, 0.2],
    }

    def __init__(self):
        self.cache: Dict[str, List[float]] = {}

    def get_word_vector(self, word: str) -> List[float]:
        """
        Returns a normalized 50-dimensional embedding vector for any word.
        Out-of-vocabulary words use deterministic hashing projection.
        """
        low = word.lower().strip()
        if low in self.cache:
            return self.cache[low]

        vec = [0.0] * self.DIM

        # If matching a seed concept, expand its seed values
        if low in self.SEED_CONCEPTS:
            seed = self.SEED_CONCEPTS[low]
            for i in range(self.DIM):
                vec[i] = seed[i % len(seed)] * (1.0 - (i / (self.DIM * 2.0)))
        else:
            # Deterministic feature hashing projection (murmur/sha256 based)
            h = hashlib.sha256(low.encode('utf-8')).digest()
            for i in range(self.DIM):
                byte_val = h[i % len(h)]
                vec[i] = ((byte_val / 128.0) - 1.0)

        normalized = VectorUtils.normalize(vec)
        self.cache[low] = normalized
        return normalized

    def embed_sentence(self, tokens: List[str]) -> List[float]:
        """Computes the unweighted mean vector of tokens in a sentence."""
        if not tokens:
            return [0.0] * self.DIM
        accum = [0.0] * self.DIM
        count = 0
        for t in tokens:
            vec = self.get_word_vector(t)
            accum = VectorUtils.add_scaled(accum, vec, 1.0)
            count += 1
        return VectorUtils.normalize(accum)
