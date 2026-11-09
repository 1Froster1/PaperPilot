"""Data contracts shared by Drogon and the Python Agent Worker.

Keep these names and JSON field names stable. The C++ service will use them
when it sends a request to this worker.
"""

from typing import Literal
AgentMode = Literal["qa", "summary", "note", "critique"]
from pydantic import BaseModel, Field


class PaperChunk(BaseModel):
    """One retrievable passage from the parsed PDF."""

    chunk_id: str
    text: str
    page_number: int | None = None
    section: str | None = None


class PaperContext(BaseModel):
    """Paper data prepared by Drogon after its permission checks."""

    title: str
    authors: str = ""
    abstract: str = ""
    tags: list[str] = Field(default_factory=list)


class PaperChatRequest(BaseModel):
    """Request body for POST /agent/paper-chat.

    user_id and paper_id come from Drogon. Qt must never call this endpoint
    directly, because Drogon is responsible for login and authorization.
    """

    request_id: str
    user_id: str
    paper_id: str
    session_id: str | None = None
    question: str = Field(min_length=1, max_length=4000)
    paper: PaperContext
    conversation: list[dict[str, str]] = Field(default_factory=list)
    mode: AgentMode = "qa"


class Citation(BaseModel):
    """A source passage used when producing the answer."""

    chunk_id: str
    text: str
    page_number: int | None = None
    section: str | None = None


class ExternalReference(BaseModel):
    """A normalized external work returned by the OpenAlex Research Agent."""

    source: str = "OpenAlex"
    openalex_id: str = ""
    title: str
    authors: list[str] = Field(default_factory=list)
    publication_year: int | None = None
    venue: str | None = None
    doi: str | None = None
    cited_by_count: int = 0
    is_open_access: bool = False
    open_access_url: str | None = None


class ToolTrace(BaseModel):
    """A record of one Agent tool call, useful for debugging and task logs."""

    tool_name: str
    summary: str


class WebSource(BaseModel):
    """One normalized web source returned by Tavily."""

    source: str = "Tavily"
    title: str
    url: str
    content: str
    score: float = 0.0
    published_date: str | None = None

class PaperChatResponse(BaseModel):
    """Response returned to Drogon, then forwarded to Qt."""

    success: bool = True
    answer: str
    citations: list[Citation] = Field(default_factory=list)
    external_references: list[ExternalReference] = Field(default_factory=list)
    tools: list[ToolTrace] = Field(default_factory=list)
    session_id: str | None = None
    status: Literal["completed", "failed"] = "completed"#`Literal["completed", "failed"]`：字面量类型，代表这个字段**只能是这两个字符串二选一**，传别的值 Pydantic 直接校验报错
    web_sources: list[WebSource] = Field(default_factory=list)

class PdfParseRequest(BaseModel):
    """Internal request sent by Drogon after it has saved an uploaded PDF."""

    task_id: str | None = None
    paper_id: str
    pdf_path: str = Field(min_length=1, description="Drogon 保存的本机 PDF 路径")
    chunk_size: int = Field(default=1000, ge=300, le=3000)
    chunk_overlap: int = Field(default=150, ge=0, le=500)


class PdfParseChunk(BaseModel):
    """One text block extracted from a PDF page."""

    chunk_index: int
    content: str
    page_start: int
    page_end: int
    section: str | None = None
    char_count: int


class PdfParseResponse(BaseModel):
    """Successful parse result returned to Drogon for database persistence."""

    success: bool = True
    status: Literal["completed"] = "completed"
    paper_id: str
    task_id: str | None = None
    page_count: int
    extracted_char_count: int
    # 从 PDF 前部文本中尽力提取的基础元数据。提取失败时保持空字符串，
    # Drogon 会保留上传时已有的标题等信息。
    extracted_title: str = ""
    extracted_authors: str = ""
    extracted_abstract: str = ""
    chunks: list[PdfParseChunk] = Field(default_factory=list)


class VectorIndexChunk(BaseModel):
    """One MySQL-persisted chunk to be embedded and indexed in Qdrant."""

    chunk_id: str = Field(min_length=1)
    chunk_index: int = Field(ge=0)
    content: str = Field(min_length=1)
    page_start: int = Field(ge=1)
    page_end: int = Field(ge=1)
    section: str | None = None


class VectorIndexRequest(BaseModel):
    """Internal request sent by Drogon only after chunks are committed to MySQL."""

    task_id: str | None = None
    paper_id: str = Field(min_length=1)
    chunks: list[VectorIndexChunk] = Field(min_length=1)
    replace_existing: bool = True


class VectorIndexResponse(BaseModel):
    """Result returned after one paper's chunks have been indexed."""

    success: bool = True
    status: Literal["completed", "failed"] = "completed"
    task_id: str | None = None
    paper_id: str
    indexed_chunk_count: int = 0
    collection_name: str


class VectorSearchHit(BaseModel):
    """One Qdrant result converted back to PaperPilot's citation shape."""

    chunk_id: str
    content: str
    score: float
    page_start: int
    page_end: int
    section: str | None = None


class VectorHealthResponse(BaseModel):
    """Safe diagnostic information for the local Qdrant integration."""

    status: Literal["ok", "unavailable"]
    qdrant_url: str
    collection_name: str
    vector_size: int
    distance: str
    detail: str = ""

