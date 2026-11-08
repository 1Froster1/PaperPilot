"""Application use cases that combine embedding generation and Qdrant storage."""

from __future__ import annotations

import logging
import time
from functools import lru_cache
from pathlib import Path

from dotenv import load_dotenv

from .embedding_service import (
    EmbeddingProvider,
    EmbeddingSettings,
    build_embedding_provider,
)
from .schemas import (
    VectorHealthResponse,
    VectorIndexRequest,
    VectorIndexResponse,
    VectorSearchHit,
)
from .vector_store import (
    PaperVectorStore,
    QdrantSettings,
    build_vector_store,
)
from .schemas import Citation

load_dotenv(Path(__file__).resolve().with_name(".env"))

logger = logging.getLogger("uvicorn.error")
logger.setLevel(logging.INFO)


class VectorServiceUnavailableError(RuntimeError):
    """The required embedding or Qdrant dependency could not serve a request."""


@lru_cache(maxsize=1)#放入缓存的
def build_vector_dependencies() -> tuple[EmbeddingProvider, PaperVectorStore]:
    """Construct the two replaceable dependencies from environment settings."""
    embedding_settings = EmbeddingSettings.from_environment()
    qdrant_settings = QdrantSettings.from_environment()

    if embedding_settings.vector_size != qdrant_settings.vector_size:
        raise ValueError(
            "Embedding dimension and QDRANT_VECTOR_SIZE must be identical."
        )

    return (
        build_embedding_provider(embedding_settings),
        build_vector_store(qdrant_settings),
    )


@lru_cache(maxsize=256)
def _embed_query_cached(question: str) -> tuple[float, ...]:
    """为这个 Worker 进程缓存重复的问题嵌入。
缓存是安全的，因为查询向量只依赖于配置的嵌入模型和问题的具体文本。当嵌入配置变化时，Worker 重启/重新加载会清空缓存。
    """
    embedding_provider, _ = build_vector_dependencies()
    return tuple(embedding_provider.embed_query(question))


def get_vector_health() -> VectorHealthResponse:
    """Check the required Qdrant service without exposing its API key.

    Failures deliberately propagate to the HTTP layer: an ``unavailable``
    health payload would make a required dependency look optional.
    """
    settings = QdrantSettings.from_environment()
    _, vector_store = build_vector_dependencies()
    vector_store.health_check()
    vector_store.ensure_collection()

    return VectorHealthResponse(
        status="ok",
        qdrant_url=settings.url,
        collection_name=settings.collection_name,
        vector_size=settings.vector_size,
        distance=settings.distance,
    )


def index_paper(request: VectorIndexRequest) -> VectorIndexResponse:
    """Embed then upsert one paper after MySQL persistence succeeds."""
    embedding_provider, vector_store = build_vector_dependencies()
    vector_store.health_check()
    vector_store.ensure_collection()

    if request.replace_existing:
        vector_store.delete_paper(request.paper_id)

    vectors = embedding_provider.embed_documents(
        [chunk.content for chunk in request.chunks]
    )
    indexed_count = vector_store.upsert_chunks(
        request.paper_id,
        request.chunks,
        vectors,
    )
    settings = QdrantSettings.from_environment()

    return VectorIndexResponse(
        task_id=request.task_id,
        paper_id=request.paper_id,
        indexed_chunk_count=indexed_count,
        collection_name=settings.collection_name,
    )



def search_paper_chunks(
    paper_id: str,
    question: str,
    limit: int = 8,
) -> list[VectorSearchHit]:
    """Semantic retrieval entry point used by agent_service.py later."""
    if limit <= 0:
        raise ValueError("Search limit must be positive.")

    try:

        _, vector_store = build_vector_dependencies()#build_vector_dependencies() 是“预热 + 校验服务
        query_vector = _embed_query_cached(question)

        # A real search is already a connectivity check. Avoid a redundant local
        # /collections round trip for every chat question.
        hits = vector_store.search(paper_id, list(query_vector), limit)

    except Exception as error:
        raise VectorServiceUnavailableError(
            "Semantic retrieval requires available Embedding and Qdrant services."
        ) from error

    return hits
