"""Paper evidence retrieval and presentation for Agent workflows."""

from __future__ import annotations

from .schemas import Citation
from .vector_service import search_paper_chunks


def retrieve_paper_evidence(
    paper_id: str,
    query: str,
    limit: int = 6,
) -> list[Citation]:
    """Retrieve semantic evidence and convert it to the Agent-facing shape."""
    hits = search_paper_chunks(paper_id, query, limit=limit)
    return [
        Citation(
            chunk_id=hit.chunk_id,
            text=hit.content,
            page_number=hit.page_start,
            section=hit.section,
        )
        for hit in hits
    ]


def format_citations(citations: list[Citation]) -> str:
    """Render citations as numbered evidence blocks for an LLM prompt."""
    if not citations:
        return "未检索到与当前问题直接相关的论文证据。"

    blocks: list[str] = []
    for index, citation in enumerate(citations, start=1):
        location: list[str] = []
        if citation.section:
            location.append(citation.section)
        if citation.page_number is not None:
            location.append(f"第 {citation.page_number} 页")
        location_text = " / ".join(location) if location else "位置未知"
        blocks.append(f"【证据 {index}｜{location_text}】\n{citation.text}")
    return "\n\n".join(blocks)
