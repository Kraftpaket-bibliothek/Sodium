# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/vector_aggregation/tfidf_graph_centrality.py
# Description: TF-IDF vector aggregation and graph centrality alignment for rules.
# ============================================================================

from __future__ import annotations
import math
from typing import List, Dict, Tuple
from ..static_vectorization.static_embeddings import StaticEmbeddingMatrix
from ..static_vectorization.vector_utils import VectorUtils


class TFIDFGraphCentrality:
    """
    Combines TF-IDF term weighting with static embeddings and builds a
    rule dependency graph to compute centrality alignment across constraints.
    """

    def __init__(self):
        self.embedding_matrix = StaticEmbeddingMatrix()

    def compute_tfidf_vectors(self, tokenized_rules: List[List[str]]) -> List[List[float]]:
        """
        Computes TF-IDF weighted centroid vectors for each rule sentence.
        """
        num_docs = len(tokenized_rules)
        if num_docs == 0:
            return []

        # Document frequencies
        df: Dict[str, int] = {}
        for rule in tokenized_rules:
            unique_words = set(w.lower() for w in rule)
            for w in unique_words:
                df[w] = df.get(w, 0) + 1

        # Calculate rule centroid vectors
        rule_vectors: List[List[float]] = []
        for rule in tokenized_rules:
            tf: Dict[str, int] = {}
            for w in rule:
                low = w.lower()
                tf[low] = tf.get(low, 0) + 1

            centroid = [0.0] * StaticEmbeddingMatrix.DIM
            total_weight = 0.0

            for w, count in tf.items():
                idf = math.log((num_docs + 1.0) / (df.get(w, 1) + 1.0)) + 1.0
                weight = count * idf
                vec = self.embedding_matrix.get_word_vector(w)
                centroid = VectorUtils.add_scaled(centroid, vec, weight)
                total_weight += weight

            normalized = VectorUtils.normalize(centroid)
            rule_vectors.append(normalized)

        return rule_vectors

    def compute_rule_centrality(self, rule_vectors: List[List[float]]) -> List[float]:
        """
        Builds a similarity graph between rules and computes normalized centrality scores.
        """
        n = len(rule_vectors)
        if n <= 1:
            return [1.0] * n

        adj = [[0.0] * n for _ in range(n)]
        for i in range(n):
            for j in range(i + 1, n):
                sim = max(0.0, VectorUtils.cosine_similarity(rule_vectors[i], rule_vectors[j]))
                adj[i][j] = sim
                adj[j][i] = sim

        degrees = [sum(row) for row in adj]
        max_deg = max(degrees) if degrees else 1.0
        if max_deg == 0.0:
            return [1.0] * n

        return [d / max_deg for d in degrees]
