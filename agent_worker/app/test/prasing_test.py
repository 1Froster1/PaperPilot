from langchain_community.document_loaders.generic import GenericLoader
from langchain_community.document_loaders.parsers import GrobidParser

loader = GenericLoader.from_filesystem(
    r"C:\Users\73644\Documents\论文\改进YOLOX算法在小目标缺陷检测中的研究与应用_胡磊.pdf",
    glob="*",
    suffixes=[".pdf"],
    parser=GrobidParser(
        segment_sentences=False,
        grobid_server="http://127.0.0.1:8070/api/processFulltextDocument",
    ),
)


# 加载文档
docs = loader.load()

# 打印第一个文档的内容
print(docs[0].page_content)

# 打印第一个文档的元数据
print(docs[0].metadata)
