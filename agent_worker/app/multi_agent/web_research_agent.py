from __future__ import annotations

import json
from typing import Any

from app.mcp_service import get_web_search_tools
from app.schemas import ToolTrace, WebSource

from .state import PaperAgentState


def _normalize_tool_result(result: Any) -> list[dict[str, Any]]:
    """兼容直接结果及 MCP text-content block 的 JSON 返回。"""

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


async def research_web(
    state: PaperAgentState,
) -> dict:
    """使用 Tavily MCP 检索与用户问题相关的最新网页资料。"""

    if "web" not in state.get("research_plan", []):
        return {}

    request = state["request"]

    tools = await get_web_search_tools()

    search_tool = next(
        (
            tool
            for tool in tools
            if tool.name.endswith("search_web")
        ),
        None,
    )

    if search_tool is None:
        return {
            "tool_traces": [
                *state.get("tool_traces", []),
                ToolTrace(
                    tool_name="tavily_search_web",
                    summary="Tavily 网络检索工具不可用。",
                ),
            ],
        }

    try:
        raw_results = await search_tool.ainvoke(
            {
                "query": request.question,
                "max_results": 5,
                "topic": "general",
            }
        )
    except Exception as error:
        return {
            "tool_traces": [
                *state.get("tool_traces", []),
                ToolTrace(
                    tool_name=search_tool.name,
                    summary=f"Tavily 检索失败：{type(error).__name__}",
                ),
            ],
        }

    web_sources: list[WebSource] = []
    error_messages: list[str] = []

    for item in _normalize_tool_result(raw_results):
        if item.get("error"):
            error_messages.append(str(item["error"]))
            continue

        try:
            web_sources.append(WebSource.model_validate(item))
        except ValueError:
            continue

    return {
        "web_sources": web_sources,
        "tool_traces": [
            *state.get("tool_traces", []),
            ToolTrace(
                tool_name=search_tool.name,
                summary=(
                    f"query={request.question!r}, "
                    f"hits={len(web_sources)}"
                    + (f", error={error_messages[0]}" if error_messages else "")
                ),
            ),
        ],
    }
