"""Agent orchestration layer.

Only function skeletons live here for now. Fill in each TODO one by one;
do not move MySQL access into this worker. Drogon remains the owner of
accounts, paper permissions, and persistent chat records.
"""

import logging
from typing import Any

from .schemas import Citation, PaperChatRequest, PaperChatResponse, ToolTrace
from langchain_core.tools import tool
from langchain_core.messages import (
    AIMessage,
    BaseMessage,
    HumanMessage,
    SystemMessage,
)
import time

from .retrieval_service import retrieve_paper_evidence
from .evidence_registry import EvidenceRegistry
from .prompt_library import build_system_prompt
from .mcp_service import get_openalex_tools
from .llm_service import create_chat_model
from app.multi_agent.graph import paper_agent_graph


# Reuse Uvicorn's configured stderr handler so application INFO logs appear in
# the same terminal as FastAPI access logs.
logger = logging.getLogger("uvicorn.error")
logger.setLevel(logging.INFO)


def format_citations(citations: list[Citation]) -> str:
    if not citations:
        return "未在当前论文中检索到与问题直接相关的原文片段。"

    formatted_parts = []

    for index,citation in enumerate(citations,start=1):
        location = []

        if citation.section:
            location.append(citation.section)

        if citation.page_number is not None:
            location.append(f"第 {citation.page_number} 页")

        location_text = " / ".join(location) if location else "位置未知"

        formatted_parts.append(f"【证据 {index}｜{location_text}】\n" f"{citation.text}")

    return "\n\n".join(formatted_parts)






def retrieve_relevant_chunks(request: PaperChatRequest) -> list[Citation]:
    """Retrieve evidence exclusively from the required semantic index."""
    return retrieve_paper_evidence(request.paper_id, request.question, limit=6)




def build_agent_tools(request: PaperChatRequest,evidence_registry: EvidenceRegistry,) -> list[Any]:
    """Create the tools that this Agent may call.

    Suggested first tools:
    - search_paper_chunks: search paragraphs in the current paper;
    - summarize_section: summarize a retrieved section;
    - draft_reading_note: produce a note draft for Qt to display.

    Tools should operate only on the already-authorized request.paper data.
    """

    @tool("summarize_section")
    def summarize_section(section: str) -> str:
        """检索并返回指定章节的相关原文，供 Agent 基于证据总结。"""
        if not section.strip():
            return "没有提供章节名称。"

        citations = retrieve_paper_evidence(
            request.paper_id, f"{section} 章节的主要内容、方法和结论", limit=8
        )

        if not citations:
            return f"未检索到与“{section}”相关的论文证据。"

        return evidence_registry.format(citations)

    @tool("draft_reading_note")
    def draft_reading_note(topic: str) -> str:
        """根据语义检索到的论文证据生成阅读笔记草稿。"""
        topic = topic.strip()
        if not topic:
            return "没有提供笔记主题。"

        citations = retrieve_paper_evidence(
            request.paper_id, f"{topic} 的核心观点、方法、结果与结论", limit=8
        )
        if not citations:
            return f"未检索到与“{topic}”相关的论文证据，无法生成可靠笔记。"

        evidence = evidence_registry.format(citations)
        prompt = f"""
            请严格根据以下论文证据生成阅读笔记。

            笔记主题：{topic}

            论文证据：
            {evidence}

            输出格式：
            1. 核心观点
            2. 论文证据
            3. 我的思考问题

            不要编造论文中不存在的信息；每一项论文证据必须保留对应的【证据编号】。
            """
        llm = create_chat_model()
        resp = llm.invoke(prompt)
        return resp.content

    tools = [
        summarize_section,
        draft_reading_note
    ]
    return tools


def build_messages(request: PaperChatRequest, citations: list[Citation]) -> list[BaseMessage]:
    """Create system, conversation-history, context, and user messages.

    TODO: require the model to answer only from the supplied paper context and
    say clearly when the paper does not contain enough evidence.
    """
    evidence_text = format_citations(citations)

    system_prompt = build_system_prompt(request.mode)

    context_prompt = f"""
    当前论文：
    标题：{request.paper.title}
    作者：{request.paper.authors}
    摘要：{request.paper.abstract}
    标签：{'、'.join(request.paper.tags)}

    当前可用论文证据：
    {evidence_text}
    """

    messages: list[BaseMessage] = [
        SystemMessage(content=system_prompt),
        SystemMessage(content=context_prompt),
    ]

    for item in request.conversation[-20:]:
        role = item.get("role")
        content = item.get("content","")#从字典 item 中读取键 "content" 对应的值；如果这个键不存在，就使用空字符串 ""

        if not content:
            continue

        if role == "user":
            messages.append(HumanMessage(content=content))
        elif role == "assistant":
            messages.append(AIMessage(content=content))

    messages.append(HumanMessage(content=request.question))

    return messages



async def run_paper_agent(request: PaperChatRequest) -> PaperChatResponse:
    """Run one paper-question Agent turn and return a structured answer.

    TODO:
    1. Retrieve relevant chunks.
    2. Build tools and messages.
    3. Invoke the LLM / LangGraph workflow.
    4. Return answer, citations, and tool traces.
    """
    if not request.question:
        return PaperChatResponse(
            success=False,
            status="failed",
            answer="没有收到用户提问"
        )
    logger.info("本轮 Agent 模式：%s", request.mode)
    logger.info("用户问题：%s", request.question)

    try:
        final_state = await paper_agent_graph.ainvoke(
            {
                "request": request,
            }#异步执行一次流程。因为 Writer 调用大模型是异步的，所以这里必须写 await
        )
    except Exception:
        logger.exception("Multi-agent paper chat failed.")
        return PaperChatResponse(
            success=False,
            status="failed",
            answer="论文 Agent 执行失败，请稍后重试。",
        )
    citations = final_state.get("internal_citations", [])
    tool_traces = final_state.get("tool_traces", [])
    answer = final_state.get("final_answer", "").strip()
    if not answer:
        logger.error("Multi-agent workflow completed without final_answer.")
        return PaperChatResponse(
            success=False,
            status="failed",
            answer="Agent 没有生成有效回答。",
            citations=citations,
            tools=tool_traces,
        )

    logger.info("多 Agent 流程完成")
    logger.info("本轮检索证据数量：%d", len(citations))
    logger.info(
        "本轮调用的工具：%s",
        [trace.tool_name for trace in tool_traces],
    )
    logger.info("回答内容为：%s", answer)

    return PaperChatResponse(
        success=True,
        status="completed",
        answer=answer,
        citations=citations,
        tools=tool_traces,
    )
    # retrieval_started_at = time.perf_counter()
    # initial_citations = retrieve_relevant_chunks(request)
    # logger.info("检索执行时间：%.3f 秒", time.perf_counter() - retrieval_started_at)
    # logger.info("收到的证据数量：%d", len(initial_citations))
    # logger.info("解析引用完成")
    #
    #
    #
    # if not initial_citations:
    #     return PaperChatResponse(
    #         success=False,
    #         status="failed",
    #         answer="当前检索到的论文证据不足以回答这个问题。",
    #         citations=[],
    #     )
    #
    # evidence_registry = EvidenceRegistry(initial_citations)
    #
    # local_tools = build_agent_tools(request, evidence_registry)
    # openalex_tools = await get_openalex_tools()
    #
    # tools_list = [
    #     *local_tools,
    #     *openalex_tools,
    # ]
    # logger.info(
    #     "本轮可用工具：%s",
    #     [tool.name for tool in tools_list],
    # )
    #
    # logger.info("工具库构造完成")
    #
    # messages = build_messages(request, evidence_registry.citations)
    #
    # logger.info("上下文构造完成")
    #
    # llm = create_chat_model()
    #
    # agent = create_agent(model=llm, tools=tools_list)
    # result = await agent.ainvoke({"messages":messages})
    # logger.info("Model completed paper chat request")
    # final_message = result["messages"][-1]
    # validation = validate_answer(final_message.content, evidence_registry.citations)
    # answer = validation.answer
    # if validation.invalid_citation_numbers:
    #     logger.warning(
    #         "Model returned invalid citation numbers: %s",
    #         validation.invalid_citation_numbers,
    #     )
    #
    #
    #
    # tool_traces = []
    #
    # for message in result["messages"]:
    #     if isinstance(message, AIMessage):
    #         for tool_call in message.tool_calls:
    #             tool_traces.append(
    #                 ToolTrace(
    #                     tool_name=tool_call["name"],
    #                     summary=f"调用参数：{tool_call['args']}"
    #                 )
    #             )
    #
    # logger.info("回答内容为：%s", answer)
    # logger.info("本轮调用的工具有：%s", [evidence_tool.tool_name for evidence_tool in tool_traces])
    #
    # return PaperChatResponse(
    #     success=True,
    #     status="completed",
    #     answer=answer,
    #     citations=evidence_registry.citations,
    #     tools=tool_traces,
    # )

def build_failure_response(error: Exception) -> PaperChatResponse:
    """Convert an internal exception into a safe response for Drogon.

    TODO: log the detailed error locally; never return API keys or stack traces
    to the Qt client.
    """
    return PaperChatResponse(
        success=False,
        status="failed",
        answer="Agent 暂时无法完成回答。",
        citations=[],
        tools=[ToolTrace(tool_name="agent", summary=type(error).__name__)],
    )
