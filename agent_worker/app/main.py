import asyncio
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException

from app.agent_service import build_failure_response, run_paper_agent
from app.pdf_worker import parse_pdf_in_worker
from app.schemas import (
    PaperChatRequest,
    PaperChatResponse,
    PdfParseRequest,
    PdfParseResponse,
    VectorHealthResponse,
    VectorIndexRequest,
    VectorIndexResponse,
)
from app.vector_service import (
    VectorServiceUnavailableError,
    build_vector_dependencies,
    get_vector_health,
    index_paper,
)

# Reuse Uvicorn's configured stderr handler for Worker diagnostics.
logger = logging.getLogger("uvicorn.error")
logger.setLevel(logging.INFO)

#uvicorn app.main:app --host 127.0.0.1 --port 8001 --reload
#docker run --rm --init --ulimit core=0 -p 8070:8070 --name paperpilot-grobid grobid/grobid:0.9.1-crf
#docker run -d --name paperpilot-qdrant --restart unless-stopped -p 127.0.0.1:6335:6333 -p 127.0.0.1:6336:6334 -v paperpilot_qdrant_data:/qdrant/storage qdrant/qdrant:latest


@asynccontextmanager
async def lifespan(app: FastAPI):
    try:
        embedding_provider, vector_store = await asyncio.to_thread(
            build_vector_dependencies
        )

        await asyncio.to_thread(vector_store.health_check)
        await asyncio.to_thread(vector_store.ensure_collection)

        # 严格验证 DashScope Key、模型、维度和网络。
        # 注意：每次 Worker 重启都会产生一次 embedding 请求。
        await asyncio.to_thread(
            embedding_provider.embed_query,
            "PaperPilot embedding health check",
        )

        logger.info("Vector services are ready.")

    except Exception as error:
        logger.exception("Required vector service startup check failed.")
        raise RuntimeError(
            "Qdrant or embedding service is unavailable; Worker cannot start."
        ) from error

    yield


app = FastAPI(
    title="PaperPilot Agent Worker",
    version="0.1.0",
    description="Internal Python Agent service called only by the Drogon backend.",
    lifespan=lifespan,
)


@app.get("/health")
async def health_check() -> dict[str, str]:
    """Health endpoint for Drogon or manual development checks."""
    return {"status": "ok", "service": "paperpilot-agent-worker"}


@app.post("/agent/paper-chat", response_model=PaperChatResponse)#这个接口返回给前端的 JSON，必须按照 `PaperChatResponse` 这个 Pydantic 模型来校验、过滤、序列化。
async def paper_chat(request: PaperChatRequest) -> PaperChatResponse:
    """Accept an authorized paper question from Drogon.

    TODO: once run_paper_agent() is implemented, Drogon can forward this
    response to Qt and separately persist the conversation in MySQL.
    """
    try:
        return await run_paper_agent(request)
    except NotImplementedError as error:
        # Skeleton stage: this makes the unfinished state explicit instead of
        # pretending that an AI answer was generated.
        raise HTTPException(status_code=501, detail=str(error)) from error
    except VectorServiceUnavailableError as error:
        logger.exception("Required semantic retrieval service is unavailable.")
        raise HTTPException(
            status_code=503,
            detail="Semantic retrieval service is temporarily unavailable.",
        ) from error
    except Exception as error:
        logger.exception("Paper Agent execution failed.")
        return build_failure_response(error)


@app.post("/worker/parse-pdf", response_model=PdfParseResponse)
async def parse_pdf(request: PdfParseRequest) -> PdfParseResponse:
    """Extract PDF text for Drogon; Drogon persists the returned chunks."""
    return await parse_pdf_in_worker(request)


@app.get("/worker/vector-health", response_model=VectorHealthResponse)
async def vector_health() -> VectorHealthResponse:
    """Check the local Qdrant connection and collection configuration."""
    try:
        return await asyncio.to_thread(get_vector_health)
    except Exception as error:
        logger.exception("Required vector service health check failed.")
        raise HTTPException(
            status_code=503,
            detail="Vector service is temporarily unavailable.",
        ) from error


@app.post("/worker/index-paper", response_model=VectorIndexResponse)
async def index_paper_vectors(request: VectorIndexRequest) -> VectorIndexResponse:
    """Embed and upsert chunks only after Drogon has committed them to MySQL."""
    try:
        return await asyncio.to_thread(index_paper, request)
    except Exception as error:
        logger.exception("Paper vector indexing failed. paper_id=%s", request.paper_id)
        raise HTTPException(
            status_code=503,
            detail="Required vector indexing service is temporarily unavailable.",
        ) from error
