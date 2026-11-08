"""LLM client construction shared by the paper agents."""

import os
from pathlib import Path

from dotenv import load_dotenv
from langchain_core.language_models.chat_models import BaseChatModel
from langchain_openai import ChatOpenAI


# Preserve the existing worker configuration location and variable names.
ENV_PATH = Path(__file__).resolve().with_name(".env")
load_dotenv(ENV_PATH)


def create_chat_model() -> BaseChatModel:
    """Create the OpenAI-compatible chat model used by all agents."""
    api_key = os.getenv("LLM_API_KEY", "").strip()
    base_url = os.getenv("LLM_BASE_URL", "").strip()
    model = os.getenv("LLM_MODEL_ID", "").strip()

    if not api_key:
        raise RuntimeError(f"LLM_API_KEY is missing; expected it in {ENV_PATH}.")
    if not model:
        raise RuntimeError(f"LLM_MODEL_ID is missing; expected it in {ENV_PATH}.")

    return ChatOpenAI(
        api_key=api_key,
        base_url=base_url or None,
        model=model,
        temperature=0.7,
        timeout=120,
        max_retries=1,
        extra_body={"thinking": {"type": "disabled"}},
    )
