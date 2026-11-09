from mcp.server.fastmcp import FastMCP
import httpx
import os
from pathlib import Path
from dotenv import load_dotenv

# This MCP server runs in its own stdio subprocess. Explicitly load the same
# Worker .env file instead of relying on the parent Uvicorn process to export it.
load_dotenv(Path(__file__).resolve().parent.parent / "app" / ".env")

mcp = FastMCP("paperpilot-web-search")

TAVILY_API_URL = os.getenv(
    "TAVILY_API_URL",
    "https://api.tavily.com/search",
).strip()
TAVILY_API_KEY = os.getenv("TAVILY_API_KEY", "").strip()
TAVILY_TIMEOUT = float(os.getenv("TAVILY_TIMEOUT", "15"))

@mcp.tool()
async def search_web(
        query: str,
        max_results: int = 5,
        topic: str = "general"
) -> list[dict]:
    query = query.strip()

    if not query:
        return []

    max_results = max(1, min(max_results, 10))

    if not TAVILY_API_KEY:
        return [
            {
                "source": "Tavily",
                "error": "未配置 TAVILY_API_KEY。",
            }
        ]

    if topic not in {"general", "news"}:
        topic = "general"

    payload = {
        "query": query,
        "max_results": max_results,
        "topic": topic,
        "search_depth": "basic",
        "include_answer": False,
        "include_raw_content": False,
        "include_images": False,
    }

    headers = {
        "Authorization": f"Bearer {TAVILY_API_KEY}",
        "Content-Type": "application/json",
    }

    try:
        async with httpx.AsyncClient(
                timeout=TAVILY_TIMEOUT,
                follow_redirects=True,
        ) as client:
            response = await client.post(
                TAVILY_API_URL,
                json=payload,
                headers=headers,
            )
            response.raise_for_status()

    except httpx.TimeoutException:
        return [
            {
                "source": "Tavily",
                "error": "Tavily 请求超时。",
            }
        ]
    except httpx.HTTPError as error:
        return [
            {
                "source": "Tavily",
                "error": f"Tavily 请求失败：{type(error).__name__}",
            }
        ]

    results: list[dict] = []

    for item in response.json().get("results", []):
        url = str(item.get("url") or "").strip()
        title = str(item.get("title") or "").strip()
        content = str(item.get("content") or "").strip()

        # 没有标题、链接或摘要的结果对 Writer 没有价值。
        if not title or not url or not content:
            continue

        results.append(
            {
                "source": "Tavily",
                "title": title,
                "url": url,
                "content": content,
                "score": float(item.get("score") or 0),
                "published_date": item.get("published_date"),
            }
        )

    return results




if __name__ == "__main__":
    mcp.run(transport="stdio")
