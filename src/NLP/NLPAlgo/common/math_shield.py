# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/common/math_shield.py
# Description: Mathematical symbol preservation and algebraic equation shielding.
#              Protects math equations, relational inequalities, and logic symbols
#              before language tokenizers & lemmatizers process the text.
# ============================================================================

from __future__ import annotations
import re
from typing import Dict, Tuple, List


class MathShield:
    """
    Detects and shields mathematical symbols and algebraic equations.
    Prevents standard natural language tokenizers and lemmatizers
    from destructively stripping relational operators and logic notation.
    """

    # Formal Unicode and standard ASCII logical / relational symbols
    SYMBOL_MAP = {
        "⟹": " -> ",
        "⇒": " -> ",
        "→": " -> ",
        "IFF": " <-> ",
        "⟺": " <-> ",
        "⇔": " <-> ",
        "↔": " <-> ",
        "∧": " AND ",
        "&": " AND ",
        "&&": " AND ",
        "∨": " OR ",
        "|": " OR ",
        "||": " OR ",
        "¬": " NOT ",
        "~": " NOT ",
        "≠": " != ",
        "≤": " <= ",
        "≥": " >= ",
    }

    # Equation / Relational patterns: e.g. x + y <= 10, A != B, Count >= 3
    EQUATION_PATTERN = re.compile(
        r'\b([a-zA-Z0-9_]+(?:\s*[+\-*/]\s*[a-zA-Z0-9_]+)*\s*(?:<=|>=|!=|==|<|>|=)\s*[a-zA-Z0-9_]+(?:\s*[+\-*/]\s*[a-zA-Z0-9_]+)*)\b'
    )

    # Implication patterns in ASCII: A -> B, A => B
    IMPLIES_PATTERN = re.compile(r'([a-zA-Z0-9_]+)\s*(?:->|=>)\s*([a-zA-Z0-9_]+)')

    # Equivalence patterns in ASCII: A <-> B, A <=> B
    EQUIV_PATTERN = re.compile(r'([a-zA-Z0-9_]+)\s*(?:<->|<=>)\s*([a-zA-Z0-9_]+)')

    def __init__(self):
        self.shield_counter = 0

    def normalize_symbols(self, text: str) -> str:
        """Converts Unicode mathematical/logical symbols to standard ASCII tokens."""
        for sym, repl in self.SYMBOL_MAP.items():
            text = text.replace(sym, repl)
        return text

    def shield(self, text: str) -> Tuple[str, Dict[str, str]]:
        """
        Substitutes equations and math expressions with safe alphanumeric tokens.
        Returns the shielded text and a dictionary mapping tokens to raw expressions.
        """
        text = self.normalize_symbols(text)
        mapping: Dict[str, str] = {}
        idx = 0

        # 1. Shield explicit algebraic equations (e.g., x + y <= 10)
        def replace_eq(m: re.Match) -> str:
            nonlocal idx
            raw = m.group(1).strip()
            # Do not shield single words or pure numbers
            if not any(op in raw for op in ["<=", ">=", "!=", "==", "<", ">", "=", "+", "-", "*", "/"]):
                return m.group(0)
            token = f"__MATH_EXPR_{idx}__"
            mapping[token] = raw
            idx += 1
            return token

        shielded = self.EQUATION_PATTERN.sub(replace_eq, text)
        return shielded, mapping

    def deshield(self, text: str, mapping: Dict[str, str]) -> str:
        """Restores shielded math placeholders back to original expressions."""
        for token, raw in mapping.items():
            text = text.replace(token, raw)
        return text

    @staticmethod
    def is_shielded_token(token: str) -> bool:
        """Returns True if token represents a shielded math or logic expression."""
        return (token.startswith("__MATH_EXPR_") or
                token.startswith("__LOGIC_IMP_") or
                token.startswith("__LOGIC_EQV_")) and token.endswith("__")
