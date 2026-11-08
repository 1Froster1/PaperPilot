"""GROBID PDF parsing Worker for PaperPilot.

Drogon calls this FastAPI worker with a local PDF path. This module sends the
PDF to GROBID, converts returned TEI XML into database-ready text chunks, and
does not write to MySQL directly.
"""

import asyncio
import logging
import os
from collections.abc import Iterator
from pathlib import Path

import fitz
import requests
from bs4 import BeautifulSoup, Tag
from fastapi import HTTPException

from .schemas import PdfParseChunk, PdfParseRequest, PdfParseResponse


logger = logging.getLogger(__name__)

GROBID_URL = os.getenv(
    "GROBID_URL",
    "http://127.0.0.1:8070/api/processFulltextDocument",
)
GROBID_TIMEOUT_SECONDS = int(os.getenv("GROBID_TIMEOUT_SECONDS", "180"))


def text_or_empty(node: Tag | None) -> str:
    """Convert one XML node to normalized plain text."""
    if node is None:
        return ""
    return " ".join(node.get_text(" ", strip=True).split())


def is_useful_metadata(value: str | None) -> bool:
    """Exclude common meaningless PDF metadata values."""
    return bool(value and value.strip() and value.strip().lower() not in {
        "untitled",
        "unknown",
        "none",
    })


def resolve_pdf_path(pdf_path: str) -> Path:
    """Resolve and validate one local PDF path received from Drogon."""
    resolved_path = Path(pdf_path).expanduser().resolve()

    if resolved_path.suffix.lower() != ".pdf":
        raise HTTPException(status_code=400, detail="Only .pdf files can be parsed.")
    if not resolved_path.is_file():
        raise HTTPException(status_code=404, detail="PDF file does not exist.")

    upload_root = os.getenv("PDF_UPLOAD_ROOT", "").strip()
    if upload_root:
        resolved_root = Path(upload_root).expanduser().resolve()
        try:
            resolved_path.relative_to(resolved_root)
        except ValueError as error:
            raise HTTPException(
                status_code=403,
                detail="PDF path is outside PDF_UPLOAD_ROOT.",
            ) from error

    return resolved_path


def read_pdf_info(pdf_path: Path) -> tuple[int, str, str]:
    """Read only page count and embedded metadata; do not extract body text."""
    try:
        with fitz.open(pdf_path) as document:
            metadata = document.metadata or {}
            return (
                len(document),
                (metadata.get("title") or "").strip(),
                (metadata.get("author") or "").strip(),
            )
    except Exception as error:
        raise HTTPException(status_code=422, detail="Unable to open PDF.") from error


def call_grobid(pdf_path: Path) -> BeautifulSoup:
    """Upload PDF binary to GROBID and parse its TEI XML response."""
    try:
        with pdf_path.open("rb") as pdf_file:
            response = requests.post(
                GROBID_URL,
                files={
                    "input": (
                        pdf_path.name,
                        pdf_file,
                        "application/pdf",
                    )
                },
                data={
                    "consolidateHeader": "0",
                    "consolidateCitations": "0",
                    "segmentSentences": "0",
                    "includeRawCitations": "0",
                    "includeRawAffiliations": "0",
                },
                timeout=GROBID_TIMEOUT_SECONDS,
            )

        response.raise_for_status()
    except requests.RequestException as error:
        logger.exception(
            "GROBID parsing failed. url=%s, pdf=%s",
            GROBID_URL,
            pdf_path,
        )
        raise HTTPException(
            status_code=502,
            detail="GROBID service is unavailable or failed to parse this PDF.",
        ) from error

    soup = BeautifulSoup(response.text, "xml")
    if soup.find("TEI") is None:
        logger.error("GROBID returned non-TEI response: %s", response.text[:500])
        raise HTTPException(
            status_code=422,
            detail="GROBID did not return valid TEI XML.",
        )

    return soup


def first_text_by_selectors(
    root: Tag | BeautifulSoup,
    selectors: list[str],
) -> str:
    """Return text from the first non-empty matching CSS selector."""
    for selector in selectors:
        value = text_or_empty(root.select_one(selector))
        if value:
            return value
    return ""


def extract_grobid_metadata(
    soup: BeautifulSoup,
    pdf_title: str,
    pdf_author: str,
    filename_stem: str,
) -> tuple[str, str, str]:
    """Extract title, authors, and abstract from GROBID TEI.

    GROBID may fail to recognize Chinese thesis cover pages or image-based
    metadata. In that case, title falls back to embedded PDF metadata and then
    the filename. Existing database author information should not be overwritten
    when this function returns an empty author string.
    """
    header = soup.find("teiHeader")

    title = ""
    authors = ""
    abstract = ""

    if header is not None:
        title = first_text_by_selectors(
            header,
            [
                "fileDesc > titleStmt > title",
                "titleStmt > title",
                "sourceDesc biblStruct analytic > title",
                "sourceDesc biblStruct monogr > title",
            ],
        )

        author_nodes = header.select(
            "sourceDesc biblStruct analytic > author"
        )
        if not author_nodes:
            author_nodes = header.find_all("author")

        author_names: list[str] = []
        seen_names: set[str] = set()

        for author_node in author_nodes:
            # Prefer only <persName>; otherwise affiliation text can be mixed in.
            person_name = text_or_empty(author_node.find("persName"))
            author_name = person_name or text_or_empty(author_node)

            if not author_name:
                continue

            normalized_key = author_name.casefold()
            if normalized_key not in seen_names:
                seen_names.add(normalized_key)
                author_names.append(author_name)

        authors = "; ".join(author_names)

        abstract = first_text_by_selectors(
            header,
            [
                "profileDesc > abstract",
                "abstract",
            ],
        )

    # GROBID sometimes writes the abstract in the body front section.
    if not abstract:
        abstract = first_text_by_selectors(
            soup,
            [
                'text > front > div[type="abstract"]',
                'text > front > div[type="summary"]',
            ],
        )

    # Use GROBID first. PDF metadata and filename are only title fallbacks.
    if not title and is_useful_metadata(pdf_title):
        title = pdf_title

    if not title:
        title = filename_stem

    if not authors and is_useful_metadata(pdf_author):
        authors = pdf_author

    return title, authors, abstract


def split_page_text(text: str, chunk_size: int, chunk_overlap: int) -> list[str]:
    """Split paragraph-aware text into bounded, overlapping chunks."""
    if not text:
        return []

    paragraphs = [part.strip() for part in text.split("\n") if part.strip()]
    chunks: list[str] = []
    current = ""

    def flush_current() -> None:
        nonlocal current
        if current:
            chunks.append(current)
            current = current[-chunk_overlap:] if chunk_overlap else ""

    for paragraph in paragraphs:
        if len(paragraph) > chunk_size:
            if current:
                flush_current()

            start = 0
            step = max(1, chunk_size - chunk_overlap)

            while start < len(paragraph):
                chunks.append(paragraph[start : start + chunk_size])
                start += step

            current = ""
            continue

        candidate = f"{current}\n{paragraph}".strip() if current else paragraph

        if len(candidate) <= chunk_size:
            current = candidate
        else:
            flush_current()
            current = paragraph

    if current:
        chunks.append(current)

    return chunks


def direct_children(parent: Tag, tag_name: str) -> Iterator[Tag]:
    """Yield direct XML children only, avoiding duplicated nested text."""
    for child in parent.children:
        if isinstance(child, Tag) and child.name == tag_name:
            yield child


def parse_page_number(marker: Tag | None, page_count: int) -> int | None:
    """Parse GROBID's optional <pb n="..."/> page-break marker."""
    if marker is None:
        return None

    raw_value = (marker.get("n") or "").strip()
    if not raw_value:
        return None

    digits = "".join(char for char in raw_value if char.isdigit())
    if not digits:
        return None

    return max(1, min(int(digits), page_count))


def get_element_page_range(element: Tag, page_count: int) -> tuple[int, int]:
    """Best-effort TEI page mapping using preceding/internal <pb> markers.

    GROBID does not emit page-break markers for every PDF. If unavailable,
    return the document range instead of inventing a specific page number.
    """
    markers: list[Tag] = []

    previous_marker = element.find_previous("pb")
    if previous_marker is not None:
        markers.append(previous_marker)

    markers.extend(element.find_all("pb"))

    page_numbers = [
        page_number
        for marker in markers
        if (page_number := parse_page_number(marker, page_count)) is not None
    ]

    if not page_numbers:
        return 1, page_count

    return page_numbers[0], page_numbers[-1]


def iter_grobid_sections(
    container: Tag,
    inherited_section: str | None = None,
) -> Iterator[tuple[str | None, str, Tag]]:
    """Yield non-duplicated paragraph text from nested GROBID body sections."""
    direct_paragraphs = [
        text_or_empty(paragraph)
        for paragraph in direct_children(container, "p")
    ]
    direct_paragraphs = [paragraph for paragraph in direct_paragraphs if paragraph]

    if direct_paragraphs:
        yield inherited_section, "\n".join(direct_paragraphs), container

    for div in direct_children(container, "div"):
        heading = text_or_empty(div.find("head", recursive=False))
        section = heading or inherited_section

        yield from iter_grobid_sections(div, section)


def parse_pdf(request: PdfParseRequest) -> PdfParseResponse:
    """Parse a PDF through GROBID and create database-ready chunks."""
    pdf_path = resolve_pdf_path(request.pdf_path)

    page_count, pdf_title, pdf_author = read_pdf_info(pdf_path)
    soup = call_grobid(pdf_path)

    extracted_title, extracted_authors, extracted_abstract = (
        extract_grobid_metadata(
            soup=soup,
            pdf_title=pdf_title,
            pdf_author=pdf_author,
            filename_stem=pdf_path.stem,
        )
    )

    print(extracted_title)
    print(extracted_authors)

    body = soup.select_one("text > body")
    if body is None:
        raise HTTPException(
            status_code=422,
            detail="GROBID TEI XML does not contain a body section.",
        )

    chunks: list[PdfParseChunk] = []
    extracted_char_count = 0
    chunk_index = 0

    for section, section_text, source_element in iter_grobid_sections(body):
        page_start, page_end = get_element_page_range(
            source_element,
            page_count,
        )

        for content in split_page_text(
            section_text,
            request.chunk_size,
            request.chunk_overlap,
        ):
            extracted_char_count += len(content)

            chunks.append(
                PdfParseChunk(
                    chunk_index=chunk_index,
                    content=content,
                    page_start=page_start,
                    page_end=page_end,
                    section=section,
                    char_count=len(content),
                )
            )
            chunk_index += 1

    if not chunks:
        raise HTTPException(
            status_code=422,
            detail="GROBID did not extract usable body text from this PDF.",
        )

    return PdfParseResponse(
        paper_id=request.paper_id,
        task_id=request.task_id,
        page_count=page_count,
        extracted_char_count=extracted_char_count,
        extracted_title=extracted_title,
        extracted_authors=extracted_authors,
        extracted_abstract=extracted_abstract,
        chunks=chunks,
    )


async def parse_pdf_in_worker(request: PdfParseRequest) -> PdfParseResponse:
    """Run blocking GROBID/PDF work outside FastAPI's event loop."""
    return await asyncio.to_thread(parse_pdf, request)