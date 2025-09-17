# your_project/graph.py

import os
from functools import partial
from dotenv import load_dotenv  # 导入 load_dotenv
from langchain.chat_models import init_chat_model
from langgraph.graph import StateGraph, END
from state import State
from tools import available_tools
from nodes import call_model, call_tool

# 在程序的早期，尽可能早地加载 .env 文件
load_dotenv()

# --- API 配置 ---
# 现在这些值是从 .env 文件加载到环境中的，我们只需确保它们存在
# LangChain 的 init_chat_model 会自动从环境变量中读取 OPENAI_API_KEY 和 OPENAI_BASE_URL
if not os.environ.get("OPENAI_API_KEY"):
    raise ValueError("OPENAI_API_KEY not found in environment variables.")

# 初始化模型并绑定工具
llm = init_chat_model("openai:gpt-4.1").bind_tools(available_tools)

# --- 节点和条件边的定义 ---

# 决策函数
def should_continue(state: State) -> str:
    last_message = state["messages"][-1]
    if last_message.tool_calls:
        return "call_tool"
    else:
        return END

# --- 构建图 ---

def get_graph_builder() -> StateGraph:
    graph_builder = StateGraph(State)

    # 使用 functools.partial 将 llm 对象绑定到 call_model 函数上
    # 这样 call_model 在图中调用时就不需要额外传递 llm 参数了
    bound_call_model = partial(call_model, llm=llm)

    # 添加节点
    graph_builder.add_node("call_model", bound_call_model)
    graph_builder.add_node("call_tool", call_tool)

    # 设置入口点
    graph_builder.set_entry_point("call_model")

    # 添加条件边
    graph_builder.add_conditional_edges(
        "call_model",
        should_continue,
        {
            "call_tool": "call_tool",
            END: END
        }
    )

    # 添加从工具节点返回模型节点的边
    graph_builder.add_edge("call_tool", "call_model")

    # 编译图
    return graph_builder