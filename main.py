# your_project/main.py
import uuid
import traceback
from langchain_core.messages import HumanMessage, ToolMessage
from langgraph.checkpoint.sqlite import SqliteSaver  

from graph import get_graph_builder # 从 graph.py 导入编译好的图实例

config = {"configurable": {"thread_id": "9308"}}
#config = {"configurable": {"thread_id": "99308"}}

def run_chat():
    """主聊天循环，负责管理 Checkpoint 上下文并运行图。"""
    
    # 使用 'with' 语句来创建和管理 Checkpointer
    # 这能确保数据库连接被安全地打开和关闭
    with SqliteSaver.from_conn_string("travel_agent.sqlite") as memory:
        
        # 1. 获取图的蓝图
        graph_builder = get_graph_builder()
        
        # 2. 在 'with' 代码块内部，使用 memory 对象来编译图
        app = graph_builder.compile(checkpointer=memory)

        while True:
            try:
                user_input = input("User: ")
                if user_input.lower() in ["quit", "exit", "q"]:
                    print("Goodbye!")
                    break
                
                # 将用户输入包装成 HumanMessage
                messages = [HumanMessage(content=user_input)]
                
                # 使用 stream 运行图
                for event in app.stream(messages, config, stream_mode="values"):
                    # 检查是否是中断事件
                    if "interrupt" in event:
                        interrupt_data = event["interrupt"]
                        print(f"\n🤖 Assistant (needs help): {interrupt_data.get('query')}")
                        
                        # 等待人类回复
                        human_response = input("Your Response: ")
                        
                        # 将人类的回复作为 ToolMessage 再次送入图，以继续执行
                        # 这会唤醒被暂停的图
                        messages = [ToolMessage(content=human_response, tool_call_id=interrupt_data.get('tool_call_id'))]
                        continue # 继续外层循环，让下一轮 stream 处理这个 ToolMessage

                    # 如果不是中断，就打印正常的AI消息
                    latest_message = event["messages"][-1]
                    if latest_message.type == "ai" and latest_message.content:
                        print("Assistant:", latest_message.content)

            except KeyboardInterrupt:
                print("\nGoodbye!")
                break
            except Exception:
                print("--- AN ERROR OCCURRED ---")
                traceback.print_exc()
                break

if __name__ == "__main__":
    run_chat()