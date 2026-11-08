"""Qdrant persistence boundary for PaperPilot's PDF chunks.

MySQL remains the source of truth. Qdrant only stores a rebuildable search
index. Keep all Qdrant-specific payload/filter details in this module.
"""

from __future__ import annotations

import os
import uuid
from dataclasses import dataclass
from typing import Protocol

from qdrant_client import QdrantClient, models

from .schemas import VectorIndexChunk, VectorSearchHit


_POINT_NAMESPACE = uuid.UUID("a14c42d2-0eb1-4fc4-8d0a-d2a2d7039ab2")


@dataclass(frozen=True)#“Qdrant 配置读入后视为只读
class QdrantSettings:
    """Non-secret local Qdrant connection and collection configuration."""

    url: str
    api_key: str | None
    collection_name: str
    vector_size: int
    distance: str
    timeout_seconds: float

    @classmethod#调用时不需要先创建对象
    def from_environment(cls) -> "QdrantSettings":
        url = os.getenv("QDRANT_URL", "").strip()
        collection_name = os.getenv("QDRANT_COLLECTION", "").strip()
        api_key = os.getenv("QDRANT_API_KEY", "").strip() or None
        vector_size = int(os.getenv("QDRANT_VECTOR_SIZE", "1024"))
        distance = os.getenv("QDRANT_DISTANCE", "cosine").strip().lower()
        timeout_seconds = float(os.getenv("QDRANT_TIMEOUT", "30"))

        if not url or not collection_name:
            raise ValueError("QDRANT_URL and QDRANT_COLLECTION are required.")
        if vector_size <= 0:
            raise ValueError("QDRANT_VECTOR_SIZE must be positive.")
        if distance != "cosine":
            raise ValueError("Only QDRANT_DISTANCE=cosine is scaffolded.")

        return cls(
            url=url.rstrip("/"),
            api_key=api_key,
            collection_name=collection_name,
            vector_size=vector_size,
            distance=distance,
            timeout_seconds=timeout_seconds,
        )


class PaperVectorStore(Protocol):
    """Operations used by indexing, retrieval, reindexing, and deletion."""

    def health_check(self) -> None:
        """Raise an exception if the configured Qdrant server is unavailable."""

    def ensure_collection(self) -> None:
        """Create the collection and a payload index for metadata.paper_id."""

    def upsert_chunks(
        self,
        paper_id: str,
        chunks: list[VectorIndexChunk],
        vectors: list[list[float]],
    ) -> int:
        """Upsert stable-ID points for one paper and return the written count."""

    def search(
        self,
        paper_id: str,
        query_vector: list[float],
        limit: int,
    ) -> list[VectorSearchHit]:
        """Search only the current paper; paper_id filtering is mandatory."""

    def delete_paper(self, paper_id: str) -> None:
        """Delete all points matching the paper_id payload filter."""


class QdrantPaperVectorStore:
    """Qdrant implementation for PaperPilot's dense chunk index."""

    def __init__(self, settings: QdrantSettings) -> None:
        self.settings = settings
        self._client = QdrantClient(
            url=settings.url,
            api_key=settings.api_key,
            timeout=settings.timeout_seconds,
        )

    def health_check(self) -> None:
        self._client.get_collections()

    def ensure_collection(self) -> None:
        if not self._client.collection_exists(self.settings.collection_name):
            self._client.create_collection(
                collection_name=self.settings.collection_name,
                vectors_config=models.VectorParams(
                    size=self.settings.vector_size,
                    distance=models.Distance.COSINE,
                ),
            )

        # paper_id is the mandatory tenancy/security filter for every query.
        self._client.create_payload_index(
            collection_name=self.settings.collection_name,
            field_name="paper_id",
            field_schema=models.PayloadSchemaType.KEYWORD,
            wait=True,
        )

    def upsert_chunks(
        self,
        paper_id: str,
        chunks: list[VectorIndexChunk],
        vectors: list[list[float]],
    ) -> int:
        if len(chunks) != len(vectors):
            raise ValueError("Chunk and vector counts must match.")

        points: list[models.PointStruct] = []
        for chunk, vector in zip(chunks, vectors, strict=True):
            if len(vector) != self.settings.vector_size:
                raise ValueError(
                    f"Chunk {chunk.chunk_index} has a {len(vector)}-dimension vector; "
                    f"expected {self.settings.vector_size}."
                )

            points.append(
                models.PointStruct(
                    id=self._point_id(paper_id, chunk.chunk_index),
                    vector=vector,
                    payload={
                        "paper_id": paper_id,
                        "chunk_id": chunk.chunk_id,
                        "chunk_index": chunk.chunk_index,
                        "content": chunk.content,
                        "page_start": chunk.page_start,
                        "page_end": chunk.page_end,
                        "section": chunk.section or "",
                    },
                )
            )

        if not points:
            return 0

        self._client.upsert(
            collection_name=self.settings.collection_name,
            points=points,
            wait=True,
        )
        return len(points)

    def search(
        self,
        paper_id: str,
        query_vector: list[float],
        limit: int,
    ) -> list[VectorSearchHit]:
        if not paper_id:
            raise ValueError("paper_id is required for vector search.")
        if len(query_vector) != self.settings.vector_size:
            raise ValueError(
                f"Query vector has {len(query_vector)} dimensions; expected "
                f"{self.settings.vector_size}."
            )

        response = self._client.query_points(
            collection_name=self.settings.collection_name,
            query=query_vector,
            query_filter=self._paper_filter(paper_id),
            limit=limit,
            with_payload=True,
            with_vectors=False,
        )

        hits: list[VectorSearchHit] = []
        for point in response.points:
            payload = point.payload or {}
            hits.append(
                VectorSearchHit(
                    chunk_id=str(payload["chunk_id"]),
                    content=str(payload["content"]),
                    score=float(point.score),
                    page_start=int(payload["page_start"]),
                    page_end=int(payload["page_end"]),
                    section=str(payload.get("section") or "") or None,
                )
            )
        return hits

    def delete_paper(self, paper_id: str) -> None:
        if not paper_id:
            raise ValueError("paper_id is required for vector deletion.")
        self._client.delete(
            collection_name=self.settings.collection_name,
            points_selector=models.FilterSelector(filter=self._paper_filter(paper_id)),
            wait=True,
        )

    @staticmethod
    def _point_id(paper_id: str, chunk_index: int) -> str:
        return str(uuid.uuid5(_POINT_NAMESPACE, f"{paper_id}:{chunk_index}"))

    @staticmethod
    def _paper_filter(paper_id: str) -> models.Filter:
        return models.Filter(
            must=[
                models.FieldCondition(
                    key="paper_id",
                    match=models.MatchValue(value=paper_id),
                )
            ]
        )


def build_vector_store(settings: QdrantSettings) -> PaperVectorStore:
    """Factory used by the service layer and tests."""
    return QdrantPaperVectorStore(settings)
