from app.schemas import PaperChatRequest, PaperChunk, PaperContext
from app.agent_service import retrieve_relevant_chunks, format_citations, build_messages
from langchain_core.messages import (
    AIMessage,
    BaseMessage,
    HumanMessage,
    SystemMessage,
)


def make_demo_request() -> PaperChatRequest:
    return PaperChatRequest(
        request_id="local-test-001",
        user_id="1",
        paper_id="100",
        question="What is the core innovation of Transformer?",
        paper=PaperContext(
            title="Attention Is All You Need",
            authors="Ashish Vaswani, Noam Shazeer",
            abstract=(
                "This paper proposes the Transformer, "
                "a model architecture based entirely on attention."
            ),
            tags=["Transformer", "Attention", "NLP"],
            chunks=[
                PaperChunk(
                    chunk_id="chunk-1",
                    text=(
                        "The Transformer uses self-attention instead of "
                        "recurrent neural networks for sequence modeling."
                    ),
                    page_number=1,
                    section="Introduction",
                ),
                PaperChunk(
                    chunk_id="chunk-2",
                    text=(
                        "Multi-head attention allows the model to attend "
                        "to information from different representation subspaces."
                    ),
                    page_number=2,
                    section="Model Architecture",
                ),
                PaperChunk(
                    chunk_id="chunk-3",
                    text=(
                        "Experiments on machine translation show that "
                        "the Transformer achieves strong BLEU scores."
                    ),
                    page_number=5,
                    section="Experiments",
                ),
                PaperChunk(
                    chunk_id="chunk-4",
                    text="The weather is sunny today and the sky is blue.",
                    page_number=8,
                    section="Appendix",
                ),
            ],
        ),
        conversation=[
            {
                "role": "user",
                "content": "请用简单语言介绍这篇论文。",
            },
            {
                "role": "assistant",
                "content": "这篇论文提出了 Transformer 架构。",
            },
        ],
    )

def test_retrieve_relevant_chunks():
    request = make_demo_request()

    citations = retrieve_relevant_chunks(request)

    assert len(citations) >= 1
    assert citations[0].chunk_id == "chunk-1"
    assert "self-attention" in citations[0].text

def test_format_citations():
    request = make_demo_request()
    citations = retrieve_relevant_chunks(request)
    evidence_text = format_citations(citations)

    assert "【证据 1" in evidence_text
    assert "Introduction" in evidence_text
    assert "第 1 页" in evidence_text
    assert "self-attention" in evidence_text


def test_build_messages():
    request = make_demo_request()
    citations = retrieve_relevant_chunks(request)
    messages = build_messages(request,citations)

    assert isinstance(messages[0], SystemMessage)
    assert "只能依据当前提供的论文证据回答" in messages[0].content

    assert isinstance(messages[1], SystemMessage)
    assert "Attention Is All You Need" in messages[1].content

    assert isinstance(messages[-1], HumanMessage)
    assert messages[-1].content == request.question


test_retrieve_relevant_chunks()
test_format_citations()
test_build_messages()