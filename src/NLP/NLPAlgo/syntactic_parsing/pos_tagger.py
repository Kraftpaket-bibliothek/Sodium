# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/syntactic_parsing/pos_tagger.py
# Description: Part-of-speech tagging and deontic modal verb classification.
# ============================================================================

from __future__ import annotations
from enum import Enum, auto
from typing import List, Tuple, Dict

try:
    import nltk
    _NLTK_AVAILABLE = True
except ImportError:
    _NLTK_AVAILABLE = False


class ModalType(Enum):
    NONE = auto()
    NECESSITY = auto()    # must, shall, always, require, mandate, need, ensure
    PROHIBITION = auto()  # cannot, must not, shall not, never, forbid, prohibit
    CONDITIONAL = auto()  # if, when, provided, assuming, suppose, unless
    DISJUNCTION = auto()  # either, or, alternatively
    CONJUNCTION = auto()  # and, both, additionally
    NEGATION = auto()     # not, no, never, neither, nor


class RulePOSTagger:
    """
    Labels grammatical categories and classifies modal auxiliaries into
    formal logic modalities.
    """

    NECESSITY_KEYWORDS = {
        "must", "shall", "always", "require", "requires", "mandate", "mandates",
        "need", "needs", "ensure", "ensures", "enforce", "enforces", "demands"
    }

    PROHIBITION_KEYWORDS = {
        "cannot", "can't", "never", "forbid", "forbids", "prohibit", "prohibits",
        "disallow", "disallows", "prevent", "prevents"
    }

    CONDITIONAL_KEYWORDS = {
        "if", "when", "whenever", "provided", "providing", "assuming", "suppose", "unless"
    }

    DISJUNCTION_KEYWORDS = {
        "either", "or", "alternatively"
    }

    CONJUNCTION_KEYWORDS = {
        "and", "both", "additionally", "furthermore"
    }

    NEGATION_KEYWORDS = {
        "not", "no", "never", "neither", "nor", "none", "without"
    }

    def tag(self, tokens: List[str]) -> List[Tuple[str, str]]:
        """
        Assigns (word, POS_TAG) tuples to a list of tokens.
        """
        if _NLTK_AVAILABLE:
            try:
                return nltk.pos_tag(tokens)
            except Exception:
                pass

        # Rule-based fallback POS tagger
        tagged = []
        for t in tokens:
            low = t.lower()
            if low in self.CONDITIONAL_KEYWORDS or low in self.CONJUNCTION_KEYWORDS or low in self.DISJUNCTION_KEYWORDS:
                tag = "CC"
            elif low in self.NEGATION_KEYWORDS:
                tag = "RB"
            elif low in self.NECESSITY_KEYWORDS or low in self.PROHIBITION_KEYWORDS:
                tag = "MD"
            elif t.startswith("__"):
                tag = "NNP"
            elif t[0].isupper():
                tag = "NNP"
            else:
                tag = "NN"
            tagged.append((t, tag))
        return tagged

    def classify_modal(self, text_or_tokens: List[str] | str) -> ModalType:
        """
        Detects the dominant deontic modal in the sentence.
        """
        if isinstance(text_or_tokens, str):
            tokens = text_or_tokens.lower().split()
        else:
            tokens = [t.lower() for t in text_or_tokens]

        text_str = " ".join(tokens)

        # Multi-word checks
        if "must not" in text_str or "shall not" in text_str or "cannot" in text_str:
            return ModalType.PROHIBITION
        if "if and only if" in text_str:
            return ModalType.CONDITIONAL

        for t in tokens:
            if t in self.PROHIBITION_KEYWORDS:
                return ModalType.PROHIBITION
            if t in self.CONDITIONAL_KEYWORDS:
                return ModalType.CONDITIONAL
            if t in self.NECESSITY_KEYWORDS:
                return ModalType.NECESSITY
            if t in self.DISJUNCTION_KEYWORDS:
                return ModalType.DISJUNCTION
            if t in self.NEGATION_KEYWORDS:
                return ModalType.NEGATION

        return ModalType.NONE
