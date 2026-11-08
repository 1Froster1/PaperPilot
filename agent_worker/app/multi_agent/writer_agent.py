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
from .state import PaperAgentState


def build_writer_messages(
    state: PaperAgentState,
) -> list[BaseMessage]:
    """只把 Research Agent 已经确认的证据交给 Writer。"""
    request = state["request"]
    citations = state.get("internal_citations", [])

    evidence_text = format_citations(citations)

    # 这里后续替换为你已经完成的多 Prompt 函数：
    # system_prompt = build_system_prompt(request.mode)
    system_prompt = f"""
你是 PaperPilot 的论文写作 Agent。

当前任务模式：{request.mode}

规则：
1. 只能依据“当前论文证据”回答论文事实。
2. 历史消息只用于理解用户当前问题中的指代。
3. 不得调用工具，不得自行搜索外部资料。
4. 证据不足时明确说明“当前检索到的论文证据不足以回答”。
5. 每个事实性结论后必须标注【证据 n】。
6. n 只能来自当前论文证据的编号。
7. 默认使用中文回答。
"""

    paper_context = f"""
当前论文：
标题：{request.paper.title}
作者：{request.paper.authors}
摘要：{request.paper.abstract}

当前论文证据：
{evidence_text}
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

    if not citations:
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
    )

    return {
        "draft_answer": validation.answer,
        "final_answer": validation.answer,
    }
