from app.schemas import Citation


class EvidenceRegistry:
    def __init__(self, citations: list[Citation] | None = None):
        self._citations: list[Citation] = []
        self._indexes_by_chunk_id: dict[str, int] = {}

        self.add_many(citations or [])

    @property
    def citations(self) -> list[Citation]:
        return list(self._citations)

    def add_many(self, citations: list[Citation]) -> list[int]:
        """添加证据并返回每条证据对应的全局编号，从 1 开始。"""
        numbers: list[int] = []

        for citation in citations:
            existing = self._indexes_by_chunk_id.get(citation.chunk_id)

            if existing is not None:
                numbers.append(existing)
                continue

            self._citations.append(citation)
            number = len(self._citations)
            self._indexes_by_chunk_id[citation.chunk_id] = number
            numbers.append(number)

        return numbers

    def format(self, citations: list[Citation]) -> str:
        """按全局编号格式化本次工具获得的证据。"""
        numbers = self.add_many(citations)
        parts: list[str] = []

        for number, citation in zip(numbers, citations):
            location: list[str] = []

            if citation.section:
                location.append(citation.section)

            if citation.page_number is not None:
                location.append(f"第 {citation.page_number} 页")

            location_text = " / ".join(location) if location else "位置未知"

            parts.append(
                f"【证据 {number}｜{location_text}】\n{citation.text}"
            )

        return "\n\n".join(parts)