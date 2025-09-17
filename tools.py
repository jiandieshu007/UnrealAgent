# your_project/tools.py

import os
import requests
import json
from langchain.tools import Tool
from dotenv import load_dotenv
from  langgraph.types import Command, interrupt

# 加载 .env 文件
load_dotenv()

@Tool
def human_assistance(query: str) -> str :
    """Request assistance from a human."""
    human_response = interrupt({"query": query})
    return human_response["data"]

# --- 1. 这是我们自定义的、可靠的搜索函数 ---
def custom_google_search(query: str) -> str:
    """
    一个直接调用 Google Serper API 的自定义搜索函数。
    它接收一个查询字符串，并返回搜索结果的字符串表示形式。
    """
    SERPER_API_KEY = os.environ.get("SERPER_API_KEY")
    if not SERPER_API_KEY:
        return "错误: SERPER_API_KEY 未在环境变量中设置。"

    url = "https://google.serper.dev/search"
    headers = {
        "X-API-KEY": SERPER_API_KEY,
        "Content-Type": "application/json"
    }
    payload = {"q": query}

    try:
        response = requests.post(url, headers=headers, json=payload)
        response.raise_for_status() # 检查HTTP错误
        
        results = response.json()
        
        # 对结果进行一些简化，以便更好地呈现给LLM
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

# --- 2. 将我们的自定义函数封装成 LangChain 工具 ---
# 我们给它和原来完全一样的名字和描述，这样模型的行为就不会改变
google_search_tool = Tool(
    name="google_search",
    description="在需要回答关于时事、最新信息或不确定的问题时，使用此工具进行Google搜索。",
    func=custom_google_search, # 注意：这里用的是我们自己的函数！
)

# --- 3. 导出工具列表和工具映射，供其他文件使用 ---
available_tools = [google_search_tool,human_assistance]
tool_map = {tool.name: tool for tool in available_tools}