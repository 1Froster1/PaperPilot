from __future__ import annotations

from .state import PaperAgentState


def initialize_turn(
    state: PaperAgentState,
) -> PaperAgentState:
    """Initialize one isolated multi-agent turn.

    Coordinator 当前只是纯 Python 节点：
    不调用 LLM、不检索 Qdrant、不调用 MCP。
    """
    request = state["request"]

    return {
        **state,
        "internal_citations": [],
        "external_references": [],
        "tool_traces": [],
        "research_summary": "",
        "draft_answer": "",
        "final_answer": "",
    }