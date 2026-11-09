from langgraph.graph import END, START, StateGraph

from app.multi_agent.coordinator import initialize_turn
from app.multi_agent.research_agent import research_current_paper
from app.multi_agent.external_research_agent import research_external_literature
from app.multi_agent.state import PaperAgentState
from app.multi_agent.writer_agent import write_answer
from app.multi_agent.web_research_agent import research_web

def build_paper_agent_graph():
    workflow = StateGraph(PaperAgentState)

    # 每个节点只负责自己的职责
    workflow.add_node("coordinator", initialize_turn)
    workflow.add_node("researcher", research_current_paper)
    workflow.add_node("external_researcher", research_external_literature)
    workflow.add_node("writer", write_answer)
    workflow.add_node("web_researcher", research_web)

    # 执行顺序
    workflow.add_edge(START, "coordinator")
    workflow.add_edge("coordinator", "researcher")
    workflow.add_edge("researcher", "external_researcher")
    workflow.add_edge("external_researcher", "web_researcher")
    workflow.add_edge("web_researcher", "writer")

    return workflow.compile()


paper_agent_graph = build_paper_agent_graph()
