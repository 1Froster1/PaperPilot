"""Load local MCP tools for PaperPilot Agents."""

from __future__ import annotations

import asyncio
import logging
import sys
from pathlib import Path

from langchain_core.tools import BaseTool
from langchain_mcp_adapters.client import MultiServerMCPClient


logger = logging.getLogger("uvicorn.error")

# agent_worker/mcp_servers/openalex_server.py
OPENALEX_SERVER_PATH = (
    Path(__file__).resolve().parent.parent
    / "mcp_servers"
    / "openalex_server.py"
)

# 只创建一次客户端配置。
# 每次 MCP tool 实际被调用时，adapter 会负责 stdio 通信。
_mcp_client = MultiServerMCPClient(
    {
        "openalex": {
            "command": sys.executable,
            "args": [str(OPENALEX_SERVER_PATH)],
            "transport": "stdio",
        }
    },
    # 自动为工具加 server 前缀，避免以后和 arXiv 等 MCP 工具重名。
    tool_name_prefix=True,
)

_openalex_tools: list[BaseTool] | None = None
_load_lock = asyncio.Lock()


WEB_SEARCH_SERVER_PATH = (
    Path(__file__).resolve().parent.parent
    / "mcp_servers"
    / "web_search_server.py"
)

_web_mcp_client = MultiServerMCPClient(
    {
        "tavily": {
            "command": sys.executable,
            "args": [str(WEB_SEARCH_SERVER_PATH)],
            "transport": "stdio",
        }
    },
    tool_name_prefix=True,
)

_web_search_tools: list[BaseTool] | None = None
_web_load_lock = asyncio.Lock()


async def get_openalex_tools() -> list[BaseTool]:
    """Load and cache OpenAlex MCP tools as LangChain tools.

    OpenAlex 是外部知识增强，不应影响已有的论文内 Qdrant 问答。
    MCP 暂时不可用时返回空工具列表，由 Agent 继续完成论文内问答。
    """
    global _openalex_tools

    if _openalex_tools is not None:
        return list(_openalex_tools)

    async with _load_lock:
        # 等待锁期间可能已有其他请求完成了加载。
        if _openalex_tools is not None:
            return list(_openalex_tools)

        if not OPENALEX_SERVER_PATH.is_file():
            logger.error(
                "OpenAlex MCP server file does not exist: %s",
                OPENALEX_SERVER_PATH,
            )
            _openalex_tools = []
            return []

        try:
            tools = await _mcp_client.get_tools()

            _openalex_tools = list(tools)

            logger.info(
                "Loaded OpenAlex MCP tools: %s",
                [tool.name for tool in _openalex_tools],
            )

        except Exception:
            # OpenAlex 失败不能让本地论文 Qdrant 问答整体失效。
            logger.exception("Failed to load OpenAlex MCP tools.")
            _openalex_tools = []

    return list(_openalex_tools)

async def get_web_search_tools() -> list[BaseTool]:
    """加载并缓存 Tavily 网络检索 MCP 工具。"""

    global _web_search_tools

    if _web_search_tools is not None:
        return list(_web_search_tools)

    async with _web_load_lock:
        if _web_search_tools is not None:
            return list(_web_search_tools)

        if not WEB_SEARCH_SERVER_PATH.is_file():
            logger.error(
                "Web search MCP server file does not exist: %s",
                WEB_SEARCH_SERVER_PATH,
            )
            _web_search_tools = []
            return []

        try:
            tools = await _web_mcp_client.get_tools()

            _web_search_tools = list(tools)

            logger.info(
                "Loaded Tavily MCP tools: %s",
                [tool.name for tool in _web_search_tools],
            )

        except Exception:
            logger.exception("Failed to load Tavily MCP tools.")
            _web_search_tools = []

    return list(_web_search_tools)