"""Embedding provider boundary for PaperPilot's retrieval pipeline.

This module intentionally contains no provider-specific HTTP implementation
yet. Implement the marked methods with the DashScope OpenAI-compatible
embeddings API after the Qdrant health check is working.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Protocol

from langchain_openai import OpenAIEmbeddings


@dataclass(frozen=True)
class EmbeddingSettings:
    """Configuration shared by document and query embedding calls."""

    provider: str
    model_name: str
    api_key: str
    base_url: str
    vector_size: int

    @classmethod
    def from_environment(cls) -> "EmbeddingSettings":
        """Read configuration without logging secrets."""
        provider = os.getenv("EMBED_MODEL_TYPE", "").strip().lower()
        model_name = os.getenv("EMBED_MODEL_NAME", "").strip()
        api_key = os.getenv("EMBED_API_KEY", "").strip()
        base_url = os.getenv("EMBED_BASE_URL", "").strip()
        vector_size = int(os.getenv("QDRANT_VECTOR_SIZE", "1024"))

        if provider != "dashscope":
            raise ValueError("Only EMBED_MODEL_TYPE=dashscope is scaffolded.")
        if not model_name or not api_key or not base_url:
            raise ValueError("EMBED_MODEL_NAME, EMBED_API_KEY, and EMBED_BASE_URL are required.")

        return cls(
            provider=provider,
            model_name=model_name,
            api_key=api_key,
            base_url=base_url,
            vector_size=vector_size,
        )


class EmbeddingProvider(Protocol):
    """Minimal contract required by the indexing and retrieval services."""

    def embed_documents(self, texts: list[str]) -> list[list[float]]:
        """Embed a batch of PDF chunks in their original order."""

    def embed_query(self, text: str) -> list[float]:
        """Embed one user question with the same model as the documents."""


class DashScopeEmbeddingProvider:
    """DashScope's OpenAI-compatible text embedding client."""

    def __init__(self, settings: EmbeddingSettings) -> None:
        self.settings = settings
        # DashScope supports at most 10 text inputs per embedding request.
        # PDF chunks are already bounded by the parsing worker, so skipping
        # tiktoken-based re-splitting preserves chunk IDs and citations.
        self._client = OpenAIEmbeddings(
            model=settings.model_name,
            openai_api_key=settings.api_key,
            openai_api_base=settings.base_url,
            dimensions=settings.vector_size,
            chunk_size=10,
            check_embedding_ctx_length=False,
            request_timeout=60,
            max_retries=1,
        )

    def embed_documents(self, texts: list[str]) -> list[list[float]]:
        if not texts:
            return []

        vectors = self._client.embed_documents(texts)
        self._validate_vectors(vectors, expected_count=len(texts))
        return vectors

    def embed_query(self, text: str) -> list[float]:
        if not text.strip():
            raise ValueError("Cannot embed an empty query.")

        vector = self._client.embed_query(text)
        self._validate_vectors([vector], expected_count=1)
        return vector

    def _validate_vectors(
        self,
        vectors: list[list[float]],
        expected_count: int,
    ) -> None:
        if len(vectors) != expected_count:
            raise RuntimeError(
                "Embedding provider returned a different number of vectors than inputs."
            )
        for index, vector in enumerate(vectors):
            if len(vector) != self.settings.vector_size:
                raise RuntimeError(
                    f"Embedding {index} has {len(vector)} dimensions; expected "
                    f"{self.settings.vector_size}."
                )


def build_embedding_provider(settings: EmbeddingSettings) -> EmbeddingProvider:
    """Factory kept here so another provider does not affect Qdrant code."""
    return DashScopeEmbeddingProvider(settings)
