
from langchain_core.messages import ToolMessage
from state import State
from tools import tool_map

def call_model(state: State, llm):
    """负责调用大模型。"""
    messages = state["messages"]
    response = llm.invoke(messages)
    return {"messages": [response]}

def call_tool(state: State):
    """负责执行工具。"""
    last_message = state["messages"][-1]
    
    if not last_message.tool_calls:
        return {}

    tool_responses = []
    for tool_call in last_message.tool_calls:
        tool_name = tool_call["name"].lower()
        if tool_name in tool_map:
            tool = tool_map[tool_name]
            tool_output = tool.invoke(tool_call["args"])
            tool_responses.append(
                ToolMessage(content=str(tool_output), tool_call_id=tool_call["id"])
            )
    
    return {"messages": tool_responses}