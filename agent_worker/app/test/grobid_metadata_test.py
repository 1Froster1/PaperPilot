"""通过 GROBID 提取论文标题和摘要的独立测试。

运行前：
1. 确认 Docker 中的 GROBID 已启动，http://127.0.0.1:8070/api/isalive 返回 true；
2. 当前 Python 环境已安装 requests、beautifulsoup4、lxml。
"""

from pathlib import Path

import requests
from bs4 import BeautifulSoup


PDF_PATH = Path(
    r"C:\Users\73644\Documents\论文\改进YOLOX算法在小目标缺陷检测中的研究与应用_胡磊.pdf"
)
GROBID_URL = "http://127.0.0.1:8070/api/processFulltextDocument"


def text_or_empty(node) -> str:
    """把 XML 节点安全转换成普通字符串；节点不存在时返回空串。"""
    return node.get_text(" ", strip=True) if node else ""


def extract_title_and_abstract(pdf_path: Path) -> tuple[str, str]:
    """上传 PDF 到 GROBID，并从返回的 TEI XML 提取标题与摘要。"""
    if not pdf_path.is_file():
        raise FileNotFoundError(f"找不到 PDF：{pdf_path}")

    # processFulltextDocument 会同时返回论文头信息和正文。
    # GROBID 的 input 字段必须是 PDF 的二进制内容，不能传本机路径字符串。
    with pdf_path.open("rb") as pdf_file:
        response = requests.post(
            GROBID_URL,
            files={"input": (pdf_path.name, pdf_file, "application/pdf")},
            data={
                "consolidateHeader": "0",
                "segmentSentences": "0",
            },
            timeout=120,
        )

    response.raise_for_status()
    soup = BeautifulSoup(response.text, "xml")

    # 标题的标准位置：TEI -> teiHeader -> fileDesc -> titleStmt -> title。
    title_node = soup.select_one("teiHeader > fileDesc > titleStmt > title")
    title = text_or_empty(title_node)

    # 摘要通常在 profileDesc 中；不同论文版式下可能被写入正文 abstract 区块，
    # 所以设置第二个兜底选择器。
    abstract_node = soup.select_one("teiHeader > profileDesc > abstract")
    if abstract_node is None or not text_or_empty(abstract_node):
        abstract_node = soup.select_one('text > front > div[type="abstract"]')
    abstract = text_or_empty(abstract_node)

    # 一些中文学位论文的封面标题是特殊字体或图片，GROBID 可能无法识别。
    # 此时回退为文件名，保证界面始终有可展示的标题；正式项目中可允许用户编辑它。
    if not title:
        title = pdf_path.stem

    return title, abstract


if __name__ == "__main__":
    title, abstract = extract_title_and_abstract(PDF_PATH)

    print("标题：", title or "（GROBID 未识别到标题）")
    print("\n摘要：")
    print(abstract or "（GROBID 未识别到摘要）")

