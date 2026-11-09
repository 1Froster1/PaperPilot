from __future__ import annotations

from .state import PaperAgentState


_OPENALEX_KEYWORDS = (
    "openalex",
    "相关论文",
    "相关工作",
    "类似论文",
    "外部文献",
    "推荐论文",
    "参考文献",
    "doi",
    "研究进展",
)

_WEB_KEYWORDS = (
    "网络检索",
    "联网检索",
    "联网搜索",
    "网络搜索",
    "网页搜索",
    "网上搜索",
    "tavily",
    "web search",
    "官网",
    "官方",
    "最新",
    "新闻",
    "公告",
    "政策",
    "版本",
    "发布",
    "项目主页",
    "github",
    "价格",
)


def choose_research_plan(question: str) -> list[str]:
    """根据用户问题决定本轮需要哪些外部能力。"""

    normalized = question.casefold()

    needs_openalex = any(
        keyword in normalized
        for keyword in _OPENALEX_KEYWORDS
    )

    needs_web = any(
        keyword in normalized
        for keyword in _WEB_KEYWORDS
    )

    # 只问外部资料时，不必浪费一次当前论文检索。
    external_only = (
        (needs_openalex or needs_web)
        and not any(
            marker in normalized
            for marker in ("这篇", "本文", "论文中", "文中", "作者")
        )
    )

    plan: list[str] = []

    if not external_only:
        plan.append("internal")

    if needs_openalex:
        plan.append("openalex")

    if needs_web:
        plan.append("web")

    # 普通问题默认只查当前论文。
    return plan or ["internal"]


def initialize_turn(
    state: PaperAgentState,
) -> PaperAgentState:
    request = state["request"]

    return {
        **state,
        "internal_citations": [],
        "external_references": [],
        "web_sources": [],
        "research_plan": choose_research_plan(request.question),
        "tool_traces": [],
        "research_summary": "",
        "draft_answer": "",
        "final_answer": "",
    }
