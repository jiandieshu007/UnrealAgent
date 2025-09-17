# your_project/state.py

from typing import Annotated
from typing_extensions import TypedDict
from langgraph.graph.message import add_messages

class State(TypedDict):
    """
    定义了图的状态。
    - messages: 一个消息列表，使用 add_messages 注解来追加新消息而不是覆盖。
    """
    messages: Annotated[list, add_messages]