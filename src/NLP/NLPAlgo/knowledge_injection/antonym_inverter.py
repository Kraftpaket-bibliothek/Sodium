# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/knowledge_injection/antonym_inverter.py
# Description: Antonym recognition and boolean literal negation mapping.
# ============================================================================

from __future__ import annotations
from typing import Dict, Optional, Tuple

try:
    from nltk.corpus import wordnet as wn
    _WN_AVAILABLE = True
except Exception:
    _WN_AVAILABLE = False


class AntonymPolarityInverter:
    """
    Identifies antonym pairs to automatically map opposing linguistic states
    to negated CDCL literals (-var_id) instead of creating redundant variables.
    """

    KNOWN_ANTONYMS: Dict[str, str] = {
        "active": "inactive",
        "inactive": "active",
        "online": "offline",
        "offline": "online",
        "enabled": "disabled",
        "disabled": "enabled",
        "running": "stopped",
        "stopped": "running",
        "up": "down",
        "down": "up",
        "permit": "forbid",
        "forbid": "permit",
        "allow": "disallow",
        "disallow": "allow",
        "succeed": "fail",
        "fail": "succeed",
        "success": "failure",
        "failure": "success",
        "true": "false",
        "false": "true",
        "valid": "invalid",
        "invalid": "valid",
        "start": "stop",
        "stop": "start",
    }

    def get_antonym(self, word: str) -> Optional[str]:
        """Returns the registered antonym for a given state word."""
        low = word.lower()
        if low in self.KNOWN_ANTONYMS:
            return self.KNOWN_ANTONYMS[low]

        if _WN_AVAILABLE:
            try:
                for syn in wn.synsets(low):
                    for lemma in syn.lemmas():
                        for ant in lemma.antonyms():
                            return ant.name().lower()
            except Exception:
                pass
        return None

    def is_antonym_pair(self, word1: str, word2: str) -> bool:
        """Returns True if word1 and word2 are polar opposites."""
        ant1 = self.get_antonym(word1)
        if ant1 and ant1 == word2.lower():
            return True
        ant2 = self.get_antonym(word2)
        if ant2 and ant2 == word1.lower():
            return True
        return False
