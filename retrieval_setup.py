import argparse
import os
import shutil
import logging
from langchain_community.document_loaders import DirectoryLoader, BSHTMLLoader
from langchain_community.vectorstores import Chroma
from langchain_huggingface import HuggingFaceEmbeddings
from langchain.text_splitter import RecursiveCharacterTextSplitter

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

def build_knowledge_base(docset_path: str, persist_directory: str, model_name: str, chunk_size: int, chunk_overlap: int):
    """
    根据传入的参数，构建并持久化本地知识库向量数据库。
    """
    # 如果持久化目录已存在，先删除，确保每次都是全新构建
    if os.path.exists(persist_directory):
        logging.info(f"正在删除已存在的数据库: {persist_directory}")
        shutil.rmtree(persist_directory)

    # 1. 数据提取与解析 (Extraction & Parsing)
    logging.info(f"正在从 {docset_path} 加载 HTML 文档...")
    loader = DirectoryLoader(
        docset_path,
        glob="**/*.html",
        loader_cls=BSHTMLLoader, # 使用 BeautifulSoup 解析 HTML
        show_progress=True,
        use_multithreading=True,
    )
    documents = loader.load()
    if not documents:
        logging.error(f"未能加载任何文档，请检查路径 {docset_path} 是否正确。")
        return
    logging.info(f"成功加载 {len(documents)} 个 HTML 页面。")

    # 2. 文本清洗与分块 (Cleaning & Chunking)
    logging.info("正在将文档分割成文本块...")
    text_splitter = RecursiveCharacterTextSplitter(
        chunk_size=chunk_size,
        chunk_overlap=chunk_overlap
    )
    docs = text_splitter.split_documents(documents)
    logging.info(f"成功将文档分割成 {len(docs)} 个文本块。")

    # 3. 文本嵌入 (Embedding)
    logging.info(f"正在初始化嵌入模型: {model_name}")
    # 第一次运行时，会自动从 Hugging Face 下载模型文件
    embeddings = HuggingFaceEmbeddings(model_name=model_name)
    logging.info("嵌入模型初始化完成。")

    # 4. 向量存储与索引 (Storage & Indexing)
    logging.info(f"正在创建向量数据库并持久化到: {persist_directory}")
    db = Chroma.from_documents(
        docs, 
        embeddings, 
        persist_directory=persist_directory
    )
    logging.info(f"知识库在 {persist_directory} 构建完成并已成功持久化！")

if __name__ == "__main__":
    # --- 命令行参数解析 ---
    parser = argparse.ArgumentParser(description="构建并持久化本地知识库向量数据库。")
    parser.add_argument("--input", required=True, help="包含HTML文件的Docset内容路径。")
    parser.add_argument("--output", required=True, help="向量数据库的持久化输出路径。")
    parser.add_argument("--model", default="all-MiniLM-L6-v2", help="要使用的Hugging Face嵌入模型名称。")
    parser.add_argument("--chunk_size", type=int, default=1000, help="文本块大小。")
    parser.add_argument("--chunk_overlap", type=int, default=200, help="文本块重叠大小。")
    
    args = parser.parse_args()

    # --- 调用带参数的函数 ---
    build_knowledge_base(
        docset_path=args.input,
        persist_directory=args.output,
        model_name=args.model,
        chunk_size=args.chunk_size,
        chunk_overlap=args.chunk_overlap
    )