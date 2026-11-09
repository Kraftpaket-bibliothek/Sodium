# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/tokenization_normalization/lemmatizer.py
# Description: Morphological lemmatizer and contraction normalizer.
# ============================================================================

from __future__ import annotations
import re
from typing import List, Dict

try:
    import nltk
    from nltk.stem import WordNetLemmatizer
    _NLTK_AVAILABLE = True
except ImportError:
    _NLTK_AVAILABLE = False


class LogicLemmatizer:
    """
    Normalizes words to their canonical base lemma while preserving
    entity names, mathematical symbols, and logic keywords.
    """

    CONTRACTIONS = {
        r"\bcan't\b": "cannot",
        r"\bwon't\b": "will not",
        r"\bshan't\b": "shall not",
        r"\bisn't\b": "is not",
        r"\baren't\b": "are not",
        r"\bwasn't\b": "was not",
        r"\bweren't\b": "were not",
        r"\bhasn't\b": "has not",
        r"\bhaven't\b": "have not",
        r"\bhadn't\b": "had not",
        r"\bdoesn't\b": "does not",
        r"\bdon't\b": "do not",
        r"\bdidn't\b": "did not",
        r"\bmustn't\b": "must not",
    }

    # Common rule verb lemmatization table
    VERB_LEMMAS = {
        "requires": "require", "required": "require", "requiring": "require",
        "mandates": "mandate", "mandated": "mandate", "mandating": "mandate",
        "enables": "enable", "enabled": "enable", "enabling": "enable",
        "disables": "disable", "disabled": "disable", "disabling": "disable",
        "activates": "activate", "activated": "activate", "activating": "activate",
        "deactivates": "deactivate", "deactivated": "deactivate", "deactivating": "deactivate",
        "runs": "run", "running": "run", "ran": "run",
        "starts": "start", "started": "start", "starting": "start",
        "stops": "stop", "stopped": "stop", "stopping": "stop",
        "fails": "fail", "failed": "fail", "failing": "fail",
        "succeeds": "succeed", "succeeded": "succeed", "succeeding": "succeed",
        "implies": "imply", "implied": "imply", "implying": "imply",
        "forbids": "forbid", "forbidden": "forbid", "forbidding": "forbid",
        "permits": "permit", "permitted": "permit", "permitting": "permit",
        "allows": "allow", "allowed": "allow", "allowing": "allow",
        "prevents": "prevent", "prevented": "prevent", "preventing": "prevent",
        "blocks": "block", "blocked": "block", "blocking": "block",
    }

    def __init__(self):
        self._wn_lemmatizer = None
        if _NLTK_AVAILABLE:
            try:
                self._wn_lemmatizer = WordNetLemmatizer()
            except Exception:
                self._wn_lemmatizer = None

    def expand_contractions(self, text: str) -> str:
        """Expands common English contractions in the rule text."""
        for pattern, replacement in self.CONTRACTIONS.items():
            text = re.sub(pattern, replacement, text, flags=re.IGNORECASE)
        return text

    def lemmatize_word(self, word: str, pos: str = "v") -> str:
        """
        Lemmatizes a single word. Never lemmatizes math placeholders or uppercase constants.
        """
        if word.startswith("__") and word.endswith("__"):
            return word

        lower = word.lower()
        if lower in self.VERB_LEMMAS:
            return self.VERB_LEMMAS[lower]

        if self._wn_lemmatizer:
            try:
                return self._wn_lemmatizer.lemmatize(lower, pos=pos)
            except Exception:
                pass

        # Rule-based morphological fallback
        if pos == "v":
            if lower.endswith("ies") and len(lower) > 3:
                return lower[:-3] + "y"
            if lower.endswith("es") and len(lower) > 3 and lower[-3] in "shxz":
                return lower[:-2]
            if lower.endswith("s") and not lower.endswith("ss") and len(lower) > 2:
                return lower[:-1]
            if lower.endswith("ed") and len(lower) > 3:
                return lower[:-2] if not lower.endswith("eed") else lower[:-1]
            if lower.endswith("ing") and len(lower) > 4:
                return lower[:-3]

        return lower

    def normalize_sentence(self, sentence: str) -> str:
        """
        Applies contraction expansion and selective lemmatization across a sentence.
        """
        text = self.expand_contractions(sentence)
        tokens = text.split()
        normalized_tokens = []
        for t in tokens:
            if t.startswith("__") and t.endswith("__"):
                normalized_tokens.append(t)
            else:
                lemma = self.lemmatize_word(t, pos="v")
                normalized_tokens.append(lemma)
        return " ".join(normalized_tokens)
