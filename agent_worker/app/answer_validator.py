"""Post-processing safeguards for evidence-grounded Agent answers."""

from __future__ import annotations

import re
from dataclasses import dataclass

from .schemas import Citation


_CITATION_PATTERN = re.compile(r"【证据\s*(\d+)】")
_EXTERNAL_REFERENCE_PATTERN = re.compile(r"【外部文献\s*(\d+)】")
_WEB_SOURCE_PATTERN = re.compile(r"【网页来源\s*(\d+)】")


@dataclass(frozen=True)
class AnswerValidation:
    answer: str
    referenced_citation_numbers: tuple[int, ...]
    invalid_citation_numbers: tuple[int, ...]


def validate_answer(
    answer: object,
    citations: list[Citation],
    external_reference_count: int = 0,
    web_source_count: int = 0,
) -> AnswerValidation:
    """Normalize model output and identify references outside this retrieval set.

    The function intentionally does not invent citations or rewrite the model's
    factual claims. It only makes invalid ``【证据 n】`` markers explicit.
    """
    normalized = str(answer or "").strip()
    if not normalized:
        normalized = "模型没有返回有效回答。"

    valid_maximum = len(citations)
    referenced = tuple(
        sorted({int(match.group(1)) for match in _CITATION_PATTERN.finditer(normalized)})
    )
    invalid = tuple(number for number in referenced if number < 1 or number > valid_maximum)

    for number in invalid:
        normalized = normalized.replace(f"【证据 {number}】", "【无效证据引用】")

    for match in _EXTERNAL_REFERENCE_PATTERN.finditer(normalized):
        number = int(match.group(1))
        if number < 1 or number > external_reference_count:
            normalized = normalized.replace(
                f"【外部文献 {number}】",
                "【无效外部文献引用】",
            )

    for match in _WEB_SOURCE_PATTERN.finditer(normalized):
        number = int(match.group(1))
        if number < 1 or number > web_source_count:
            normalized = normalized.replace(
                f"【网页来源 {number}】",
                "【无效网页来源引用】",
            )

    return AnswerValidation(
        answer=normalized,
        referenced_citation_numbers=tuple(
            number for number in referenced if number not in invalid
        ),
        invalid_citation_numbers=invalid,
    )
