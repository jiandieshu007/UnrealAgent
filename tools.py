# your_project/tools.py

import os
import requests
import json
import logging
from langchain.tools import Tool
from dotenv import load_dotenv
from langgraph.types import Command, interrupt

# --- 新增 RAG 相关导入 ---
from langchain_community.vectorstores import Chroma
from langchain_huggingface import HuggingFaceEmbeddings # 使用新的推荐包
from langchain.tools.retriever import create_retriever_tool

# --- 配置部分 ---
# 加载 .env 文件，确保 SERPER_API_KEY 等环境变量被加载
load_dotenv()

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')


# --- RAG 知识库工具 ---

# 1. RAG 工具的配置
CPP_DB_PATH = "ue_cpp_chroma_db"
BLUEPRINT_DB_PATH = "ue_blueprint_chroma_db"
EMBEDDING_MODEL_NAME = "all-MiniLM-L6-v2"

# 2. 初始化嵌入模型 (只需一次)
try:
    embeddings = HuggingFaceEmbeddings(model_name=EMBEDDING_MODEL_NAME)
except Exception as e:
    logging.error(f"无法初始化嵌入模型，请确保 'sentence-transformers' 和 'langchain-huggingface' 已安装: {e}")
    embeddings = None

# 3. 创建检索器工具的函数
def create_rag_tools():
    if not embeddings:
        logging.error("嵌入模型未成功加载，无法创建 RAG 工具。")
        return []

    # 加载 C++ 知识库
    try:
        cpp_vectorstore = Chroma(persist_directory=CPP_DB_PATH, embedding_function=embeddings)
        cpp_retriever = cpp_vectorstore.as_retriever(search_kwargs={"k": 5}) # 每次检索5个最相关的块
        logging.info(f"成功加载 C++ 知识库: {CPP_DB_PATH}")
    except Exception as e:
        logging.error(f"加载 C++ 知识库失败: {CPP_DB_PATH} - {e}")
        cpp_retriever = None

    # 加载 Blueprint 知识库
    try:
        blueprint_vectorstore = Chroma(persist_directory=BLUEPRINT_DB_PATH, embedding_function=embeddings)
        blueprint_retriever = blueprint_vectorstore.as_retriever(search_kwargs={"k": 5})
        logging.info(f"成功加载 Blueprint 知识库: {BLUEPRINT_DB_PATH}")
    except Exception as e:
        logging.error(f"加载 Blueprint 知识库失败: {BLUEPRINT_DB_PATH} - {e}")
        blueprint_retriever = None

    rag_tools = []
    if cpp_retriever:
        tool_cpp_retriever = create_retriever_tool(
            cpp_retriever,
            "unreal_engine_cpp_api_retriever",
            "当用户问题明确涉及Unral中的C++代码、API函数、类、UCLASS、UFUNCTION、USTRUCT、.h或.cpp文件时，使用此工具在虚幻引擎C++ API文档中进行语义搜索。"
        )
        rag_tools.append(tool_cpp_retriever)
        
    if blueprint_retriever:
        tool_blueprint_retriever = create_retriever_tool(
            blueprint_retriever,
            "unreal_engine_blueprint_api_retriever",
            "当用户问题涉及Unreal中的蓝图节点、事件图、函数库、Kismet、可视化脚本或宏时，使用此工具在虚幻引擎蓝图API文档中进行语义搜索。"
        )
        rag_tools.append(tool_blueprint_retriever)
        
    return rag_tools

# --- 现有工具 ---

@Tool
def human_assistance(query: str) -> str :
    """当 Agent 卡住或需要人类决策时，请求人类协助。"""
    human_response = interrupt({"query": query})
    return human_response["data"]

def custom_google_search(query: str) -> str:
    """
    一个直接调用 Google Serper API 的自定义搜索函数。
    它接收一个查询字符串，并返回搜索结果的字符串表示形式。
    """
    SERPER_API_KEY = os.environ.get("SERPER_API_KEY")
    if not SERPER_API_KEY:
        return "错误: SERPER_API_KEY 未在环境变量中设置。"
    # ... (您之前的 custom_google_search 函数的内部逻辑保持不变) ...
    url = "https://google.serper.dev/search"
    headers = {
        "X-API-KEY": SERPER_API_KEY,
        "Content-Type": "application/json"
    }
    payload = {"q": query}
    try:
        response = requests.post(url, headers=headers, json=payload)
        response.raise_for_status()
        results = response.json()
        if "organic" in results:
            return json.dumps(results["organic"], ensure_ascii=False, indent=2)
        elif "answerBox" in results:
            return json.dumps(results["answerBox"], ensure_ascii=False, indent=2)
        else:
            return "未找到相关搜索结果。"
    except requests.exceptions.HTTPError as http_err:
        return f"HTTP 错误: {http_err} - 响应内容: {response.text}"
    except Exception as e:
        return f"调用搜索工具时发生未知错误: {e}"

google_search_tool = Tool(
    name="google_search",
    description="在需要回答关于时事、最新信息、或本地知识库无法回答的通用问题时，使用此工具进行Google搜索。",
    func=custom_google_search,
)

# --- 导出最终的工具列表 ---
# 合并所有工具
rag_tools_list = create_rag_tools()
other_tools_list = [google_search_tool, human_assistance]
available_tools = rag_tools_list + other_tools_list

# 2. 根据最终的工具列表创建工具映射
tool_map = {tool.name: tool for tool in available_tools}

# 3. 打印日志确认
logging.info(f"成功加载并导出 {len(available_tools)} 个工具: {[tool.name for tool in available_tools]}")