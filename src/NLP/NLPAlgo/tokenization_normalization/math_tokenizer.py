# ============================================================================
# Sodium - Deterministic Classical NLP Methodology
# File: src/NLP/NLPAlgo/tokenization_normalization/math_tokenizer.py
# Description: Math-aware tokenization and sentence boundary segmentation.
# ============================================================================

from __future__ import annotations
import re
from dataclasses import dataclass
from typing import List, Dict, Tuple
from ..common.math_shield import MathShield


class LogicalTokenizationError(ValueError):
    """Raised when input cannot be tokenized under the controlled logic grammar."""


@dataclass(frozen=True)
class ExpressionToken:
    kind: str
    value: str
    start: int
    end: int


class MathTokenizer:
    """
    Splits unstructured text into discrete rule sentences and tokens
    while safeguarding mathematical equations, relational bounds, and logic symbols.
    """

    def __init__(self):
        self.shield = MathShield()

    _EXPRESSION_TOKEN = re.compile(
        r'(?P<QUOTED>"(?:\\.|[^"\\])*")'
        r'|(?P<IFF><->|<=>|\bif\s+and\s+only\s+if\b|\biff\b)'
        r'|(?P<IMPLIES>->|=>|\bimplies\b)'
        r'|(?P<AND>&&|&|\band\b|∧)'
        r'|(?P<OR>\|\||\||\bor\b|∨)'
        r'|(?P<NOT>!|~|¬|\bnot\b)'
        r'|(?P<IF>\bif\b)'
        r'|(?P<THEN>\bthen\b)'
        r'|(?P<LPAREN>\()'
        r'|(?P<RPAREN>\))',
        re.IGNORECASE,
    )
    _UNSUPPORTED_CONSTRUCTION = re.compile(
        r'\b(unless|xor|either|must|shall|should|cannot|always|never|eventually|'
        r'until|forall|exists|every|each|some|probably|likely|probability)\b',
        re.IGNORECASE,
    )

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
            sub_sentences = re.split(r'\.\s+(?=\S)', line)
            for s in sub_sentences:
                clean_s = s.strip().rstrip('.')
                if clean_s:
                    rules.append(clean_s)

        return rules, math_map

    def tokenize_expression(self, expression: str) -> List[ExpressionToken]:
        """Tokenizes the controlled logical grammar while retaining source spans."""
        if not isinstance(expression, str) or not expression.strip():
            raise LogicalTokenizationError("Cannot tokenize an empty logical expression.")

        text = self.shield.normalize_symbols(expression)
        if len(re.findall(r'(?<!\\)(?:\\\\)*"', text)) % 2:
            raise LogicalTokenizationError("Unterminated quoted proposition.")

        # In requirement statements, "must be" is assertion wording, not a
        # modal operator. Normalize only this explicit, restricted surface form.
        modal_forms = re.compile(
            r'(?P<quoted>"(?:\\.|[^"\\])*")'
            r'|(?P<negative>\bmust\s+not\s+be\b)'
            r'|(?P<positive>\bmust\s+be\b)',
            re.IGNORECASE,
        )
        text = modal_forms.sub(
            lambda match: match.group("quoted") or ("is not" if match.group("negative") else "is"),
            text,
        )

        # A comma can introduce the consequent in the supported "If A, B" form.
        if re.match(r'^\s*if\b', text, re.IGNORECASE) and not re.search(r'\bthen\b', text, re.IGNORECASE):
            comma = text.find(',')
            if comma >= 0:
                text = text[:comma] + " THEN " + text[comma + 1:]

        unquoted = re.sub(r'"(?:\\.|[^"\\])*"', ' ', text)
        unsupported = self._UNSUPPORTED_CONSTRUCTION.search(unquoted)
        if unsupported:
            raise LogicalTokenizationError(
                f"Unsupported logical construction '{unsupported.group(0)}' at position {unsupported.start()}."
            )

        tokens: List[ExpressionToken] = []
        segment_start = 0

        def append_atom(start: int, end: int) -> None:
            raw = text[start:end]
            left_trimmed = len(raw) - len(raw.lstrip())
            right_trimmed = len(raw.rstrip())
            if right_trimmed <= left_trimmed:
                return
            atom = raw[left_trimmed:right_trimmed]
            atom_start = start + left_trimmed
            atom_end = start + right_trimmed
            tokens.append(ExpressionToken("ATOM", atom, atom_start, atom_end))

        for match in self._EXPRESSION_TOKEN.finditer(text):
            kind = match.lastgroup or ""

            # In a copular negative sentence, "not" belongs to the proposition
            # phrase and is converted into a NOT node by the parser.
            if kind == "NOT":
                prefix = text[segment_start:match.start()]
                if re.search(r'\b(?:is|are|was|were)\s+$', prefix, re.IGNORECASE):
                    continue

            if kind == "QUOTED":
                append_atom(segment_start, match.start())
                value = match.group()[1:-1]
                value = value.replace('\\"', '"').replace('\\\\', '\\')
                if not value.strip():
                    raise LogicalTokenizationError("Quoted propositions cannot be empty.")
                tokens.append(ExpressionToken("ATOM", value, match.start(), match.end()))
                segment_start = match.end()
                continue

            append_atom(segment_start, match.start())
            tokens.append(ExpressionToken(kind, match.group(), match.start(), match.end()))
            segment_start = match.end()

        append_atom(segment_start, len(text))
        if not tokens:
            raise LogicalTokenizationError("The expression contains no proposition or logical operator.")
        return tokens

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
