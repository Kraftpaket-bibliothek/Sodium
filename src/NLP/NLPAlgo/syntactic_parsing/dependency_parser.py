# ============================================================================
# Sodium - Classical Non-LLM NLP Methodology
# File: src/NLP/NLPAlgo/syntactic_parsing/dependency_parser.py
# Description: Syntactic dependency analysis and logical clause segmentation.
# ============================================================================

from __future__ import annotations
import re
from typing import Optional, Tuple
from ..common.logic_types import LogicalOp, ParsedRule


class LogicalDependencyParser:
    """
    Parses syntactic relationships to segment complex rules into
    antecedents (conditions), consequents (effects), and logical operators.
    """

    # 1. Conditionals: "If A then B", "If A, B"
    IF_THEN_PATTERN = re.compile(
        r'^\s*if\s+(.+?)\s*(?:,\s*then\s*|\s+then\s*|,\s*)(.+)$',
        re.IGNORECASE
    )

    # 2. Inverted Conditionals: "B if A"
    B_IF_A_PATTERN = re.compile(
        r'^\s*(.+?)\s+if\s+(.+)$',
        re.IGNORECASE
    )

    # 3. Exceptions: "A unless B"
    UNLESS_PATTERN = re.compile(
        r'^\s*(.+?)\s+unless\s+(.+)$',
        re.IGNORECASE
    )

    # 4. Implication: "A requires B", "A implies B", "A necessitates B"
    REQUIRES_PATTERN = re.compile(
        r'^\s*(.+?)\s+(?:requires?|implies?|necessitates?|demands?)\s+(.+)$',
        re.IGNORECASE
    )

    # 5. Mutual Exclusion: "A and B cannot be active together", "A and B are mutually exclusive"
    MUTEX_PATTERN_1 = re.compile(
        r'^\s*(?:either\s+)?(.+?)\s+and\s+(.+?)\s+(?:are\s+mutually\s+exclusive|cannot\s+(?:both\s+)?(?:be\s+)?(?:active|running|true|online|coexist|co-exist)|never\s+coexist)$',
        re.IGNORECASE
    )
    MUTEX_PATTERN_2 = re.compile(
        r'^\s*mutually\s+exclusive\s*:\s*(.+?)\s*,\s*(.+)$',
        re.IGNORECASE
    )

    # 6. Bi-conditional / Equivalence: "A if and only if B", "A is equivalent to B"
    EQUIV_PATTERN = re.compile(
        r'^\s*(.+?)\s+(?:if\s+and\s+only\s+if|is\s+equivalent\s+to|<->|<=>)\s+(.+)$',
        re.IGNORECASE
    )

    # 7. Exact Choice: "Choose exactly one of A or B", "Exactly one of A, B"
    EXACT_ONE_PATTERN = re.compile(
        r'^\s*(?:choose\s+)?exactly\s+one\s+of\s+(.+?)\s+(?:or|,)\s+(.+)$',
        re.IGNORECASE
    )

    # 8. Unconditional Prohibition: "Never allow A", "A is prohibited", "Cannot have A"
    PROHIBIT_PATTERN = re.compile(
        r'^\s*(?:never\s+(?:allow|enable|run)?|prohibit(?:ed)?|cannot\s+have|disallow(?:ed)?)\s+(.+)$',
        re.IGNORECASE
    )

    # 9. Mandatory: "Always require A", "Always enable A", "Ensure A"
    MANDATORY_PATTERN = re.compile(
        r'^\s*(?:always\s+(?:require|enable|ensure|run)?|ensure|mandate|require)\s+(.+)$',
        re.IGNORECASE
    )

    def parse_rule(self, rule_id: int, sentence: str) -> ParsedRule:
        """
        Decomposes a single rule sentence into its logical components.
        """
        raw = sentence.strip()

        # Check for shielded symbolic implication: A -> B
        if " -> " in raw:
            parts = raw.split(" -> ", 1)
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=parts[0].strip(),
                consequent=parts[1].strip(),
                op=LogicalOp.IMPLIES
            )

        # Check for shielded symbolic equivalence: A <-> B
        if " <-> " in raw:
            parts = raw.split(" <-> ", 1)
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=parts[0].strip(),
                consequent=parts[1].strip(),
                op=LogicalOp.EQUIV
            )

        # 1. If A then B
        m = self.IF_THEN_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=m.group(1).strip(),
                consequent=m.group(2).strip(),
                op=LogicalOp.IMPLIES
            )

        # 2. A if and only if B
        m = self.EQUIV_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=m.group(1).strip(),
                consequent=m.group(2).strip(),
                op=LogicalOp.EQUIV
            )

        # 3. A unless B (equivalent to: A OR B, or NOT B -> A)
        m = self.UNLESS_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                consequent=m.group(1).strip(),
                antecedent=m.group(2).strip(),
                op=LogicalOp.OR
            )

        # 4. Mutual Exclusion
        m = self.MUTEX_PATTERN_1.match(raw) or self.MUTEX_PATTERN_2.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=m.group(1).strip(),
                consequent=m.group(2).strip(),
                op=LogicalOp.MUTEX
            )

        # 5. Exactly one of A or B
        m = self.EXACT_ONE_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=m.group(1).strip(),
                consequent=m.group(2).strip(),
                op=LogicalOp.XOR
            )

        # 6. A requires B
        m = self.REQUIRES_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=m.group(1).strip(),
                consequent=m.group(2).strip(),
                op=LogicalOp.IMPLIES
            )

        # 7. B if A (inverted conditional)
        m = self.B_IF_A_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                antecedent=m.group(2).strip(),
                consequent=m.group(1).strip(),
                op=LogicalOp.IMPLIES
            )

        # 8. Unconditional Prohibition: "Cannot have A", "Never allow A"
        m = self.PROHIBIT_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                consequent=m.group(1).strip(),
                op=LogicalOp.NOT,
                is_negative=True
            )

        # 9. Mandatory assertion: "Always require A"
        m = self.MANDATORY_PATTERN.match(raw)
        if m:
            return ParsedRule(
                rule_id=rule_id,
                raw_text=raw,
                consequent=m.group(1).strip(),
                op=LogicalOp.FACT,
                is_mandatory=True
            )

        # Default fallback: atomic statement or isolated fact
        return ParsedRule(
            rule_id=rule_id,
            raw_text=raw,
            consequent=raw,
            op=LogicalOp.FACT
        )
