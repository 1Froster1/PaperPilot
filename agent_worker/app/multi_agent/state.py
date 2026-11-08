from __future__ import annotations

from typing import TypedDict

from app.schemas import (
    Citation,
    PaperChatRequest,
    ToolTrace,
)


class PaperAgentState(TypedDict, total=False):
    """One multi-agent paper-analysis turn's shared state."""

    request: PaperChatRequest

    # 本轮从当前论文 Qdrant 中检索得到的证据。
    internal_citations: list[Citation]

    # 本轮 MCP/OpenAlex 得到的外部文献。
    external_references: list[dict]

    # 所有 Agent 的工具调用记录。
    tool_traces: list[ToolTrace]

    # Research Agent 后续写入。
    research_summary: str

    # Writer Agent 后续写入。
    draft_answer: str
    final_answer: str