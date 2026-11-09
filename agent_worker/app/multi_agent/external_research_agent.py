"""External-literature Research Agent backed by the local OpenAlex MCP."""

from __future__ import annotations

import json
from typing import Any

from app.mcp_service import get_openalex_tools
from app.schemas import ExternalReference, ToolTrace

from .state import PaperAgentState


def _normalize_tool_result(result: Any) -> list[dict[str, Any]]:
    """Decode direct values and MCP text-content blocks into dictionaries."""

    def decode(value: Any) -> list[dict[str, Any]]:
        if isinstance(value, list):
            decoded: list[dict[str, Any]] = []
            for item in value:
                decoded.extend(decode(item))
            return decoded
        if isinstance(value, dict):
            if value.get("type") == "text" and isinstance(value.get("text"), str):
                return decode(value["text"])
            return [value]
        if not isinstance(value, str):
            return []
        try:
            decoded = json.loads(value)
        except json.JSONDecodeError:
            return []
        return decode(decoded)

    return decode(result)


async def research_external_literature(state: PaperAgentState) -> dict:
    """Search OpenAlex only when Coordinator selected external research."""
    if "openalex" not in state.get("research_plan", []):
        return {}

    request = state["request"]
    tools = await get_openalex_tools()
    search_tool = next(
        (tool for tool in tools if tool.name.endswith("search_works")),
        None,
    )
    if search_tool is None:
        return {
            "tool_traces": [
                *state.get("tool_traces", []),
                ToolTrace(
                    tool_name="openalex_search_works",
                    summary="OpenAlex MCP 工具不可用。",
                ),
            ]
        }

    try:
        raw_results = await search_tool.ainvoke(
            {"query": request.question, "limit": 5}
        )
    except Exception as error:
        return {
            "tool_traces": [
                *state.get("tool_traces", []),
                ToolTrace(
                    tool_name=search_tool.name,
                    summary=f"OpenAlex 检索失败：{type(error).__name__}",
                ),
            ]
        }

    references: list[ExternalReference] = []
    for item in _normalize_tool_result(raw_results):
        if item.get("error") or not item.get("title"):
            continue
        try:
            references.append(ExternalReference.model_validate(item))
        except ValueError:
            continue

    return {
        "external_references": references,
        "tool_traces": [
            *state.get("tool_traces", []),
            ToolTrace(
                tool_name=search_tool.name,
                summary=f"query={request.question!r}, hits={len(references)}",
            ),
        ],
        "research_summary": (
            f"{state.get('research_summary', '')} "
            f"OpenAlex 返回 {len(references)} 篇外部文献。"
        ).strip(),
    }
