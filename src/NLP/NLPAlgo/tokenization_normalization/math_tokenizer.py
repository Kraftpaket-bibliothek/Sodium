# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/tokenization_normalization/math_tokenizer.py
# Description: Math-aware tokenization and sentence boundary segmentation.
# ============================================================================

from __future__ import annotations
import re
from typing import List, Dict, Tuple
from ..common.math_shield import MathShield


class MathTokenizer:
    """
    Splits unstructured text into discrete rule sentences and tokens
    while safeguarding mathematical equations, relational bounds, and logic symbols.
    """

    def __init__(self):
        self.shield = MathShield()

    def split_rules(self, raw_text: str) -> Tuple[List[str], Dict[str, str]]:
        """
        Shields math and splits input into individual rule propositions.
        Delimiters include newlines, semicolons, and periods followed by whitespace.
        Numbered labels (e.g. 'Rule 1:', '1.') are cleanly stripped.
        """
        # 1. Shield math and logic equations
        shielded_text, math_map = self.shield.shield(raw_text)

        # 2. Split into candidate lines by newline or semicolon
        lines = re.split(r'[\n;]+', shielded_text)
        rules: List[str] = []

        for line in lines:
            line = line.strip()
            if not line:
                continue

            # Strip leading rule prefixes like 'Rule 1:', '1.', '- '
            line = re.sub(r'^(?:Rule\s*\d+\s*[:.-]?|\d+[\.\)]|\-\s*|\*\s*)', '', line, flags=re.IGNORECASE).strip()
            if not line:
                continue

            # Further split on periods that terminate full sentences
            sub_sentences = re.split(r'\.\s+(?=[A-Z0-9_]|__MATH|__LOGIC)', line)
            for s in sub_sentences:
                clean_s = s.strip().rstrip('.')
                if clean_s:
                    rules.append(clean_s)

        return rules, math_map

    def tokenize_words(self, rule_str: str) -> List[str]:
        """
        Tokenizes words while keeping shielded math placeholders intact.
        """
        tokens: List[str] = []
        raw_tokens = re.findall(r'__MATH_EXPR_\d+__|__LOGIC_IMP_\d+__|__LOGIC_EQV_\d+|[a-zA-Z0-9_\-]+|[^\s\w]', rule_str)
        for t in raw_tokens:
            t = t.strip()
            if t:
                tokens.append(t)
        return tokens
