from mcp.server.fastmcp import FastMCP
import httpx
import os

mcp = FastMCP("paperpilot-openalex")

OPENALEX_BASE_URL = os.getenv(
    "OPENALEX_BASE_URL",
    "https://api.openalex.org",
).rstrip("/")

OPENALEX_API_KEY = os.getenv("OPENALEX_API_KEY", "").strip()
OPENALEX_MAILTO = os.getenv("OPENALEX_MAILTO", "").strip()
OPENALEX_TIMEOUT = float(os.getenv("OPENALEX_TIMEOUT", "15"))

@mcp.tool()
async def search_works(
    query: str,
    limit: int = 5,
    from_year: int | None = None,
    to_year: int | None = None,
    open_access_only: bool = False,
) -> list[dict]:
    query = query.strip()

    if not query:
        return []

    limit = max(1,min(limit,10))
    filters: list[str] = []
    if from_year is not None:
        filters.append(f"from_publication_date:{from_year}-01-01")

    if to_year is not None:
        filters.append(f"to_publication_date:{to_year}-12-31")

    if open_access_only:
        filters.append("is_oa:true")

    params: dict[str, str | int] = {
        "search": query,
        "per_page": limit,
    }

    if filters:
        params["filter"] = ",".join(filters)

    # API Key 不是首版必需；配置后会使用更高配额。
    if OPENALEX_API_KEY:
        params["api_key"] = OPENALEX_API_KEY

    # OpenAlex 建议标明客户端联系方式。
    if OPENALEX_MAILTO:
        params["mailto"] = OPENALEX_MAILTO

    try:
        async with httpx.AsyncClient(
            timeout=OPENALEX_TIMEOUT,
            follow_redirects=True,
        ) as client:
            response = await client.get(
                f"{OPENALEX_BASE_URL}/works",
                params=params,
            )
            response.raise_for_status()

    except httpx.TimeoutException:
        return [{
            "source": "OpenAlex",
            "error": "OpenAlex 请求超时。",
        }]
    except httpx.HTTPError as error:
        return [{
            "source": "OpenAlex",
            "error": f"OpenAlex 请求失败：{type(error).__name__}",
        }]

    works: list[dict] = []

    for item in response.json().get("results", []):
        best_oa_location = item.get("best_oa_location") or {}
        primary_location = item.get("primary_location") or {}
        source = primary_location.get("source") or {}

        authors = [
            authorship.get("author", {}).get("display_name")
            for authorship in item.get("authorships", [])
            if authorship.get("author", {}).get("display_name")
        ]

        works.append(
            {
                "source": "OpenAlex",
                "openalex_id": item.get("id", "").rsplit("/", 1)[-1],
                "title": item.get("display_name") or "未提供标题",
                "authors": authors[:10],
                "publication_year": item.get("publication_year"),
                "publication_date": item.get("publication_date"),
                "venue": source.get("display_name"),
                "doi": item.get("doi"),
                "cited_by_count": item.get("cited_by_count", 0),
                "is_open_access": (item.get("open_access") or {}).get(
                    "is_oa",
                    False,
                ),
                "open_access_url": (
                    best_oa_location.get("pdf_url")
                    or best_oa_location.get("landing_page_url")
                ),
            }
        )

    return works


@mcp.tool()
async def get_work_by_doi(doi: str) -> dict:
    """根据 DOI 查询一篇论文的 OpenAlex 元数据。"""
    doi = doi.strip()

    if not doi:
        return {
            "source": "OpenAlex",
            "error": "没有提供 DOI。",
        }

    # 支持用户输入以下任意形式：
    # 10.1000/example
    # doi:10.1000/example
    # https://doi.org/10.1000/example
    doi = doi.removeprefix("doi:").strip()
    doi = doi.removeprefix("DOI:").strip()

    if doi.startswith("http://") or doi.startswith("https://"):
        doi = doi.rstrip("/").rsplit("/", 1)[-1]

    doi_url = f"https://doi.org/{doi}"

    params: dict[str, str] = {}

    if OPENALEX_API_KEY:
        params["api_key"] = OPENALEX_API_KEY

    if OPENALEX_MAILTO:
        params["mailto"] = OPENALEX_MAILTO

    try:
        async with httpx.AsyncClient(
            timeout=OPENALEX_TIMEOUT,
            follow_redirects=True,
        ) as client:
            response = await client.get(
                f"{OPENALEX_BASE_URL}/works/{doi_url}",
                params=params,
            )

    except httpx.TimeoutException:
        return {
            "source": "OpenAlex",
            "doi": doi_url,
            "error": "OpenAlex 请求超时。",
        }
    except httpx.HTTPError as error:
        return {
            "source": "OpenAlex",
            "doi": doi_url,
            "error": f"OpenAlex 请求失败：{type(error).__name__}",
        }

    if response.status_code == 404:
        return {
            "source": "OpenAlex",
            "doi": doi_url,
            "error": "OpenAlex 中未找到该 DOI 对应的论文。",
        }

    try:
        response.raise_for_status()
        item = response.json()
    except (httpx.HTTPError, ValueError):
        return {
            "source": "OpenAlex",
            "doi": doi_url,
            "error": "OpenAlex 返回了无法解析的数据。",
        }

    best_oa_location = item.get("best_oa_location") or {}
    primary_location = item.get("primary_location") or {}
    source = primary_location.get("source") or {}

    authors = [
        authorship.get("author", {}).get("display_name")
        for authorship in item.get("authorships", [])
        if authorship.get("author", {}).get("display_name")
    ]

    return {
        "source": "OpenAlex",
        "openalex_id": item.get("id", "").rsplit("/", 1)[-1],
        "title": item.get("display_name") or "未提供标题",
        "authors": authors[:10],
        "publication_year": item.get("publication_year"),
        "publication_date": item.get("publication_date"),
        "venue": source.get("display_name"),
        "doi": item.get("doi") or doi_url,
        "cited_by_count": item.get("cited_by_count", 0),
        "is_open_access": (item.get("open_access") or {}).get(
            "is_oa",
            False,
        ),
        "open_access_url": (
            best_oa_location.get("pdf_url")
            or best_oa_location.get("landing_page_url")
        ),
    }


if __name__ == "__main__":
    # This process communicates exclusively over stdin/stdout with the
    # LangChain MCP adapter. Do not print normal logs to stdout here.
    mcp.run(transport="stdio")
