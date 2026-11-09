from __future__ import annotations

from .state import PaperAgentState
from app.retrieval_service import retrieve_paper_evidence
from app.schemas import ToolTrace


def research_current_paper(
    state: PaperAgentState,
) -> dict:
    """Research Agent 第一版：只检索当前论文的 Qdrant 证据。"""
    request = state["request"]

    if "internal" not in state.get("research_plan", ["internal"]):
        return {
            "research_summary": "本轮不需要检索当前论文。",
        }

    citations = retrieve_paper_evidence(
        paper_id=request.paper_id,
        query=request.question,
        limit=6,
    )

    tool_traces = [
        *state.get("tool_traces", []),
        ToolTrace(
            tool_name="qdrant_retrieve",
            summary=(
                f"paper_id={request.paper_id}, "
                f"query={request.question!r}, "
                f"hits={len(citations)}"
            ),
        ),
    ]

    return {
        "internal_citations": citations,
        "tool_traces": tool_traces,
        "research_summary": (
            f"已从当前论文检索到 {len(citations)} 条相关证据。"
        ),
    }
