# your_project/retrieval_setup.py

import os
from langchain_community.document_loaders import PlaywrightURLLoader # 导入新的加载器
from langchain.text_splitter import RecursiveCharacterTextSplitter
from langchain_openai import OpenAIEmbeddings
from langchain_chroma import Chroma
from dotenv import load_dotenv

load_dotenv()
if not os.environ.get("OPENAI_API_KEY"):
    raise ValueError("OPENAI_API_KEY not found in environment variables.")

# --- 1. 手动定义一组我们想要抓取的关键页面 URL ---
# 我们可以从您截图的左侧导航栏中挑选几个
urls = [
    "https://dev.epicgames.com/documentation/zh-cn/unreal-engine/understanding-the-basics-of-unreal-engine", # 管理基础知识
    "https://dev.epicgames.com/documentation/zh-cn/unreal-engine/unreal-engine-5-migration-guide",         # 迁移指南
    "https://dev.epicgames.com/documentation/zh-cn/unreal-engine/working-with-unreal-engine-source-from-github" # 使用C++
]
print(f"准备从 {len(urls)} 个指定URL加载文档...")

# --- 2. 使用 PlaywrightURLLoader 来加载这些页面 ---
# remove_selectors=["header", "footer"] 会尝试移除页面顶部的导航栏和底部的页脚，以获取更干净的内容
loader = PlaywrightURLLoader(urls=urls, remove_selectors=["header", "footer"])

print("正在使用浏览器加载页面 (这可能需要一些时间)...")
docs = loader.load()
print(f"文档加载完毕，共抓取到 {len(docs)} 个页面。")

# --- 3. 切分文档 (代码不变) ---
print("正在切分所有文档...")
text_splitter = RecursiveCharacterTextSplitter(chunk_size=1000, chunk_overlap=200)
splits = text_splitter.split_documents(docs)
print(f"文档切分完毕，共 {len(splits)} 个片段。")

# --- 4. 创建并持久化向量数据库 (代码不变) ---
print("正在创建和保存向量数据库...")
persist_directory = 'chroma_db_unreal_engine'

vectorstore = Chroma.from_documents(
    documents=splits,
    embedding=OpenAIEmbeddings(),
    persist_directory=persist_directory
)
print(f"向量数据库创建成功，并已保存至 '{persist_directory}' 文件夹。")