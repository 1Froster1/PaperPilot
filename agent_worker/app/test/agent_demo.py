from app.agent_service import run_paper_agent
from app.schemas import PaperChatRequest, PaperChunk, PaperContext
import asyncio

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

async def main():
    request = make_demo_request()

    response = await run_paper_agent(request)

    #model_dump_json()：Pydantic v2 的方法，将模型转成 JSON；
    #indent=2：每层 JSON 缩进 2 个空格，让输出更易读；
    print(response.model_dump_json(indent=2))


if __name__ == "__main__":
    asyncio.run(main())