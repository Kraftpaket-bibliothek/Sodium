# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/entity_extraction/pattern_extractor.py
# Description: Rule-based regex pattern extraction for logical entities and states.
# ============================================================================

from __future__ import annotations
import re
from typing import List, Tuple, Dict, Set


class EntityPatternExtractor:
    """
    Extracts atomic propositional components, entities, and state predicates
    from clause fragments.
    """

    # Copula and state phrases to isolate core entities and determine polarity
    POSITIVE_STATE_PATTERNS = [
        re.compile(r'\b(?:must\s+be|shall\s+be|can\s+be|should\s+be|be|is|are|was|were)?\s*(?:active|online|enabled|running|operational|up|true|valid|satisfied)\b', re.IGNORECASE),
        re.compile(r'\b(?:runs|starts|succeeds)\b', re.IGNORECASE),
    ]

    NEGATIVE_STATE_PATTERNS = [
        re.compile(r'\b(?:must\s+not\s+be|cannot\s+be|shall\s+not\s+be|be|is|are|was|were)?\s*(?:not\s+(?:active|online|enabled|running|operational|up|true)|inactive|offline|disabled|stopped|down|false|invalid)\b', re.IGNORECASE),
        re.compile(r'\b(?:fails|crashes|breaks|stops)\b', re.IGNORECASE),
        re.compile(r'\b(?:not|cannot|never)\b', re.IGNORECASE),
    ]

    # Filler words to strip when cleaning entity names
    STOP_WORDS = {
        "the", "a", "an", "this", "that", "these", "those",
        "must", "should", "shall", "always", "can", "will", "would",
        "be", "is", "are", "was", "were", "been", "being",
        "have", "has", "had", "do", "does", "did",
        "to", "of", "in", "for", "on", "with", "at", "by",
        "then", "so", "also"
    }

    def clean_entity_name(self, text: str) -> str:
        """
        Cleans an entity string by stripping noise words, state predicates, and punctuation.
        """
        t = text.strip()
        # Remove state descriptions
        for pat in self.POSITIVE_STATE_PATTERNS + self.NEGATIVE_STATE_PATTERNS:
            t = pat.sub('', t)

        # Remove common modal verbs
        t = re.sub(r'\b(?:must|shall|should|can|will|always|never|cannot)\b', '', t, flags=re.IGNORECASE)

        # Tokenize and filter out filler words while keeping uppercase/math tokens and designations
        raw_words = re.findall(r'[a-zA-Z0-9_]+', t)
        words = []
        for i, w in enumerate(raw_words):
            # If 'A' or number is a designation suffix (e.g. 'Server A', 'Node 1'), preserve it
            if (w == "A" or w.isdigit()) and i > 0:
                words.append(w)
            elif w.lower() in self.STOP_WORDS:
                continue
            else:
                words.append(w)

        if not words:
            return text.strip()

        # Join into canonical CamelCase / snake_case name
        clean_name = "_".join(words)
        return clean_name

    def extract_entity_and_polarity(self, clause_fragment: str) -> Tuple[str, bool]:
        """
        Extracts the primary entity name and its asserted polarity (True = positive, False = negated).
        """
        raw = clause_fragment.strip()

        # Check for explicit negation
        is_negative = False
        for pat in self.NEGATIVE_STATE_PATTERNS:
            if pat.search(raw):
                is_negative = True
                break

        # Check if the fragment is a shielded math placeholder
        if raw.startswith("__MATH_EXPR_") or raw.startswith("__LOGIC_"):
            return raw, not is_negative

        clean = self.clean_entity_name(raw)
        return clean, not is_negative
