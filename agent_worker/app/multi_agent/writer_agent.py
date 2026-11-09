from __future__ import annotations

from langchain_core.messages import (
    AIMessage,
    BaseMessage,
    HumanMessage,
    SystemMessage,
)

from app.llm_service import create_chat_model
from app.answer_validator import validate_answer
from app.retrieval_service import format_citations
from app.prompt_library import build_system_prompt
from .state import PaperAgentState


def build_writer_messages(
    state: PaperAgentState,
) -> list[BaseMessage]:
    """只把 Research Agent 已经确认的证据交给 Writer。"""
    request = state["request"]
    citations = state.get("internal_citations", [])
    external_references = state.get("external_references", [])
    web_sources = state.get("web_sources", [])

    evidence_text = format_citations(citations)

    system_prompt = build_system_prompt(request.mode) + """

你是工作流中的 Writer Agent，只能根据下方给出的材料写作。
当前论文的事实必须标注【证据 n】；外部检索到的书目信息必须标注
【外部文献 n】；网络网页资料必须标注【网页来源 n】。
网页资料和外部文献已经由 Research Agent 检索完成，应当在相关时使用它们。
你不能自行调用工具；历史消息只用于理解指代，不能作为事实依据。
不要编造未提供的论文、数据或结论。
"""

    external_text = "未执行外部文献检索。"
    if external_references:
        external_text = "\n".join(
            (
                f"【外部文献 {index}】{reference.title}"
                f"（{', '.join(reference.authors) or '作者未知'}，"
                f"{reference.publication_year or '年份未知'}；"
                f"{reference.venue or '来源未知'}；"
                f"DOI: {reference.doi or '无'}）"
            )
            for index, reference in enumerate(external_references, start=1)
        )

    web_text = "未执行网络检索。"
    if web_sources:
        web_text = "\n\n".join(
            (
                f"【网页来源 {index}】{source.title}\n"
                f"链接：{source.url}\n"
                f"内容：{source.content}"
            )
            for index, source in enumerate(web_sources, start=1)
        )

    paper_context = f"""
当前论文：
标题：{request.paper.title}
作者：{request.paper.authors}
摘要：{request.paper.abstract}

当前论文证据：
{evidence_text}

外部文献：
{external_text}

网页资料：
{web_text}
"""

    messages: list[BaseMessage] = [
        SystemMessage(content=system_prompt),
        SystemMessage(content=paper_context),
    ]

    for item in request.conversation[-20:]:
        role = item.get("role")
        content = item.get("content", "").strip()

        if not content:
            continue

        if role == "user":
            messages.append(HumanMessage(content=content))
        elif role == "assistant":
            messages.append(AIMessage(content=content))

    messages.append(HumanMessage(content=request.question))

    return messages


async def write_answer(
    state: PaperAgentState,
) -> dict:
    """Writer Agent：只根据 Research Agent 的内部论文证据生成答案。"""
    citations = state.get("internal_citations", [])
    external_references = state.get("external_references", [])
    web_sources = state.get("web_sources", [])

    if not citations and not external_references and not web_sources:
        return {
            "draft_answer": "",
            "final_answer": "当前检索到的论文证据不足以回答这个问题。",
        }

    messages = build_writer_messages(state)

    llm = create_chat_model()

    # 注意：直接调模型，不使用 create_agent()，因此 Writer 没有工具。
    response = await llm.ainvoke(messages)

    validation = validate_answer(
        response.content,
        citations,
        external_reference_count=len(external_references),
        web_source_count=len(web_sources),
    )

    return {
        "draft_answer": validation.answer,
        "final_answer": validation.answer,
    }
