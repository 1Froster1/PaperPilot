from langgraph.graph import END, START, StateGraph

from app.multi_agent.coordinator import initialize_turn
from app.multi_agent.research_agent import research_current_paper
from app.multi_agent.state import PaperAgentState
from app.multi_agent.writer_agent import write_answer


def build_paper_agent_graph():
    workflow = StateGraph(PaperAgentState)

    # 每个节点只负责自己的职责
    workflow.add_node("coordinator", initialize_turn)
    workflow.add_node("researcher", research_current_paper)
    workflow.add_node("writer", write_answer)

    # 执行顺序
    workflow.add_edge(START, "coordinator")
    workflow.add_edge("coordinator", "researcher")
    workflow.add_edge("researcher", "writer")
    workflow.add_edge("writer", END)

    return workflow.compile()


paper_agent_graph = build_paper_agent_graph()