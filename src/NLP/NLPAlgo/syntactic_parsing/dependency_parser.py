"""Recursive-descent parser for Sodium's controlled propositional grammar."""

from __future__ import annotations

import re
from typing import List, Sequence

from ..common.logic_types import LogicalExpression, LogicalOp
from ..tokenization_normalization.math_tokenizer import ExpressionToken


class LogicalParseError(ValueError):
    """Raised when the token stream is not a valid supported expression."""


class LogicalDependencyParser:
    """Builds an ILR expression tree; it does not assign variables or emit CNF."""

    def parse(self, tokens: Sequence[ExpressionToken], source: str = "") -> LogicalExpression:
        if not tokens:
            raise LogicalParseError("Expression has no tokens.")
        self.tokens = list(tokens)
        self.source = source
        self.index = 0
        expression = self._parse_iff()
        if self.index != len(self.tokens):
            token = self.tokens[self.index]
            raise LogicalParseError(
                f"Unexpected token {token.value!r} at position {token.start}; "
                "operators require an expression on both sides."
            )
        expression.validate()
        return expression

    def _current(self) -> ExpressionToken | None:
        return self.tokens[self.index] if self.index < len(self.tokens) else None

    def _accept(self, *kinds: str) -> ExpressionToken | None:
        token = self._current()
        if token is not None and token.kind in kinds:
            self.index += 1
            return token
        return None

    def _expect(self, kind: str, message: str) -> ExpressionToken:
        token = self._accept(kind)
        if token is None:
            current = self._current()
            position = current.start if current else len(self.source)
            raise LogicalParseError(f"{message} (position {position}).")
        return token

    def _parse_iff(self) -> LogicalExpression:
        left = self._parse_implication()
        while self._accept("IFF"):
            right = self._parse_implication()
            left = self._node(LogicalOp.EQUIV, left, right)
        return left

    def _parse_implication(self) -> LogicalExpression:
        if self._accept("IF"):
            antecedent = self._parse_iff()
            self._expect("THEN", "IF requires THEN before its consequent")
            consequent = self._parse_implication()
            return self._node(LogicalOp.IMPLIES, antecedent, consequent)

        left = self._parse_or()
        if self._accept("IMPLIES"):
            right = self._parse_implication()
            return self._node(LogicalOp.IMPLIES, left, right)
        return left

    def _parse_or(self) -> LogicalExpression:
        operands = [self._parse_and()]
        while self._accept("OR"):
            operands.append(self._parse_and())
        if len(operands) == 1:
            return operands[0]
        return self._node(LogicalOp.OR, *operands)

    def _parse_and(self) -> LogicalExpression:
        operands = [self._parse_unary()]
        while self._accept("AND"):
            operands.append(self._parse_unary())
        if len(operands) == 1:
            return operands[0]
        return self._node(LogicalOp.AND, *operands)

    def _parse_unary(self) -> LogicalExpression:
        not_token = self._accept("NOT")
        if not_token:
            operand = self._parse_unary()
            return LogicalExpression(
                op=LogicalOp.NOT,
                operands=(operand,),
                source=self.source[not_token.start:operand.end or not_token.end],
                start=not_token.start,
                end=operand.end,
            )
        return self._parse_primary()

    def _parse_primary(self) -> LogicalExpression:
        open_token = self._accept("LPAREN")
        if open_token:
            expression = self._parse_iff()
            close_token = self._expect("RPAREN", "Missing closing parenthesis")
            return LogicalExpression(
                op=expression.op,
                operands=expression.operands,
                proposition=expression.proposition,
                source=self.source[open_token.start:close_token.end],
                start=open_token.start,
                end=close_token.end,
            )

        token = self._accept("ATOM")
        if token is None:
            current = self._current()
            position = current.start if current else len(self.source)
            raise LogicalParseError(f"Expected a proposition or parenthesized expression at position {position}.")

        # The controlled English form "X is not Y" is exactly NOT (X is Y).
        copular_negative = re.fullmatch(
            r"(.+?)\s+(is|are|was|were)\s+not\s+(.+)", token.value, re.IGNORECASE
        )
        if copular_negative:
            positive_phrase = " ".join(
                (copular_negative.group(1), copular_negative.group(2), copular_negative.group(3))
            )
            atom = self._atom(positive_phrase, token.start, token.end)
            return LogicalExpression(
                op=LogicalOp.NOT,
                operands=(atom,),
                source=token.value,
                start=token.start,
                end=token.end,
            )
        return self._atom(token.value, token.start, token.end)

    def _atom(self, proposition: str, start: int, end: int) -> LogicalExpression:
        cleaned = " ".join(proposition.split())
        if not cleaned:
            raise LogicalParseError(f"Empty proposition at position {start}.")
        return LogicalExpression(
            op=LogicalOp.FACT,
            proposition=cleaned,
            source=self.source[start:end] if self.source else cleaned,
            start=start,
            end=end,
        )

    def _node(self, op: LogicalOp, *operands: LogicalExpression) -> LogicalExpression:
        start = operands[0].start
        end = operands[-1].end
        source = self.source[start:end] if self.source and start is not None and end is not None else ""
        return LogicalExpression(op=op, operands=tuple(operands), source=source, start=start, end=end)
