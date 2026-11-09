# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/lexicon_scoring/textrank_extractor.py
# Description: Graph-based TextRank keyphrase and entity centrality extraction.
# ============================================================================

from __future__ import annotations
import math
from typing import List, Dict, Tuple, Set


class TextRankKeyphraseExtractor:
    """
    Constructs a lexical co-occurrence graph over rule sentences and runs
    PageRank to extract high-centrality keyphrases and prominent predicates.
    """

    STOP_WORDS = {
        "the", "a", "an", "is", "are", "was", "were", "be", "to", "of",
        "and", "or", "in", "for", "on", "with", "at", "by", "from",
        "then", "so", "if", "that", "this", "it"
    }

    def __init__(self, window_size: int = 3, damping: float = 0.85, max_iter: int = 25):
        self.window_size = window_size
        self.damping = damping
        self.max_iter = max_iter

    def extract_keyphrases(self, tokens: List[str], top_n: int = 5) -> List[Tuple[str, float]]:
        """
        Runs TextRank power iteration over token co-occurrences.
        Returns top_n (phrase, score) pairs.
        """
        filtered = [t.lower() for t in tokens if t.lower() not in self.STOP_WORDS and len(t) > 1]
        if not filtered:
            return []

        vocab: List[str] = list(set(filtered))
        node_to_idx = {word: i for i, word in enumerate(vocab)}
        n = len(vocab)
        if n == 1:
            return [(vocab[0], 1.0)]

        # Build adjacency matrix (co-occurrence within window)
        adj: List[List[float]] = [[0.0] * n for _ in range(n)]
        for i, word in enumerate(filtered):
            idx1 = node_to_idx[word]
            for j in range(max(0, i - self.window_size), min(len(filtered), i + self.window_size + 1)):
                if i != j:
                    idx2 = node_to_idx[filtered[j]]
                    adj[idx1][idx2] += 1.0

        # Degree normalization
        out_degrees = [sum(row) for row in adj]

        # Power iteration for PageRank
        scores = [1.0 / n] * n
        for _ in range(self.max_iter):
            new_scores = [(1.0 - self.damping) / n] * n
            for i in range(n):
                for j in range(n):
                    if adj[j][i] > 0.0 and out_degrees[j] > 0.0:
                        new_scores[i] += self.damping * (adj[j][i] / out_degrees[j]) * scores[j]
            scores = new_scores

        ranked = sorted(zip(vocab, scores), key=lambda x: x[1], reverse=True)
        return ranked[:top_n]
