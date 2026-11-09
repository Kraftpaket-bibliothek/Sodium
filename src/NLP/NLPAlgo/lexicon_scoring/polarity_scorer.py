# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/lexicon_scoring/polarity_scorer.py
# Description: VADER-aligned requirement polarity and constraint intensity scoring.
# ============================================================================

from __future__ import annotations
from typing import Dict

try:
    from nltk.sentiment.vader import SentimentIntensityAnalyzer
    _VADER_AVAILABLE = True
except Exception:
    _VADER_AVAILABLE = False


class RequirementPolarityScorer:
    """
    Evaluates rule intensity, prohibitive negativity, and requirement polarity
    using VADER sentiment principles tailored to formal specifications.
    """

    # Domain intensity adjustments
    INTENSITY_LEXICON: Dict[str, float] = {
        # Strongly prohibitive / conflict keywords
        "forbidden": -3.5,
        "prohibited": -3.5,
        "conflict": -3.0,
        "crash": -3.2,
        "fatal": -3.5,
        "illegal": -3.0,
        "cannot": -2.5,
        "never": -2.8,
        "disallowed": -2.5,
        "impossible": -2.8,

        # Strongly mandatory / invariant keywords
        "mandatory": 3.0,
        "invariant": 3.2,
        "always": 2.5,
        "guaranteed": 2.8,
        "enforce": 2.5,
        "required": 2.5,
        "verified": 2.2,
        "safe": 2.0,
    }

    def __init__(self):
        self._vader = None
        if _VADER_AVAILABLE:
            try:
                self._vader = SentimentIntensityAnalyzer()
                # Update VADER lexicon with constraint terms
                self._vader.lexicon.update(self.INTENSITY_LEXICON)
            except Exception:
                self._vader = None

    def score_rule(self, text: str) -> Dict[str, float]:
        """
        Returns compound polarity score (-1.0 to +1.0) and constraint weight.
        """
        if self._vader:
            try:
                scores = self._vader.polarity_scores(text)
                return {
                    "compound": scores.get("compound", 0.0),
                    "pos": scores.get("pos", 0.0),
                    "neg": scores.get("neg", 0.0),
                    "neu": scores.get("neu", 1.0)
                }
            except Exception:
                pass

        # Lightweight rule-based scoring fallback
        tokens = text.lower().split()
        score = 0.0
        for t in tokens:
            if t in self.INTENSITY_LEXICON:
                score += self.INTENSITY_LEXICON[t]

        # Normalize score into [-1.0, 1.0]
        compound = max(-1.0, min(1.0, score / 5.0))
        return {
            "compound": compound,
            "pos": max(0.0, compound),
            "neg": max(0.0, -compound),
            "neu": 1.0 - abs(compound)
        }
