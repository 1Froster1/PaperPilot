from app.multi_agent.coordinator import initialize_turn
from app.schemas import PaperChatRequest, PaperContext
from app.multi_agent.coordinator import initialize_turn


request = PaperChatRequest(
    request_id="test-request-001",
    user_id="1",
    paper_id="38",
    question="YOLO 的核心思想是什么？",
    mode="qa",
    paper=PaperContext(
        title="You Only Look Once",
        authors="Joseph Redmon, Ali Farhadi",
        abstract="一篇关于实时目标检测的论文。",
        tags=["YOLO", "目标检测", "计算机视觉"],
    ),
    conversation=[
        {
            "role": "user",
            "content": "这篇论文研究什么？",
        },
        {
            "role": "assistant",
            "content": "它研究实时目标检测方法。",
        },
    ],
)

state = initialize_turn(
    {
        "request": request,
    }
)

from app.multi_agent.research_agent import research_current_paper

research_result = research_current_paper(state)

state.update(research_result)

print(state["research_summary"])

for index, citation in enumerate(
    state["internal_citations"],
    start=1,
):
    print(
        f"【证据 {index}】"
        f"{citation.section} / 第 {citation.page_number} 页"
    )
    print(citation.text[:100])