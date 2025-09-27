# 文件名: meshy_handler.py
# 最终专业版本 - 支持独立的资产文件夹和纹理下载

import requests
import time
import os
import json
from urllib.parse import urlparse

class MeshyClient:
    def __init__(self, api_key):
        # ... (这部分代码保持不变) ...
        if not api_key:
            raise ValueError("API key is required.")
        self.api_key = api_key
        self.base_url = "https://api.meshy.ai/openapi/v2"
        self.base_headers = {
            "Authorization": f"Bearer {self.api_key}"
        }

    def _post_request(self, endpoint, payload):
        # ... (这部分代码保持不变) ...
        url = f"{self.base_url}/{endpoint}"
        response = requests.post(url, headers=self.base_headers, json=payload)
        response.raise_for_status()
        return response.json()

    def _wait_for_task_completion_stream(self, task_id):
        # ... (这个高效的流式查询函数保持不变) ...
        stream_url = f"{self.base_url}/text-to-3d/{task_id}/stream"
        stream_headers = self.base_headers.copy()
        stream_headers["Accept"] = "text/event-stream"
        print(f"Opening stream to track task {task_id}...")
        try:
            with requests.get(stream_url, headers=stream_headers, stream=True, timeout=600) as response:
                response.raise_for_status()
                for line in response.iter_lines():
                    if not line or not line.startswith(b'data:'):
                        continue
                    try:
                        data_str = line.decode('utf-8')[5:]
                        data = json.loads(data_str)
                        status = data.get("status")
                        progress = data.get("progress", 0)
                        print(f"Task {task_id}: Status is '{status}', Progress: {progress}%")
                        if status == "SUCCEEDED":
                            print("Task completed successfully!")
                            return data
                        elif status in ["FAILED", "CANCELED"]:
                            error_msg = data.get("task_error", {}).get("message", "Task failed/canceled.")
                            print(f"Task failed. Error: {error_msg}")
                            return None
                    except (json.JSONDecodeError, IndexError):
                        print(f"Warning: Could not parse stream data: {line}")
        except requests.exceptions.RequestException as e:
            print(f"Error connecting to stream: {e}")
            return None
        return None

    def _download_asset_bundle(self, asset_folder_path, model_url, texture_urls, name):
        """
        下载一个完整的资产包（模型 + 纹理）到指定的文件夹。

        :param asset_folder_path: 资产的专用文件夹路径 (e.g., 'output/my_awesome_model_task123')
        :param model_url: 主模型文件的URL。
        :param texture_urls: 包含所有纹理URL的列表。
        :return: 下载好的主模型文件的完整路径。
        """
        print(f"Creating asset folder at: {asset_folder_path}")
        os.makedirs(asset_folder_path, exist_ok=True)
        
        # 1. 下载主模型文件
        model_filename = name
        model_file_path = os.path.join(asset_folder_path, model_filename)
        print(f"Downloading model to {model_file_path}...")
        response = requests.get(model_url)
        response.raise_for_status()
        with open(model_file_path, "wb") as f:
            f.write(response.content)

        # 2. 下载所有纹理文件
        if texture_urls:
            print("Downloading textures...")
            for texture_group in texture_urls:
                for texture_type, texture_url in texture_group.items():
                    # 从URL中提取原始文件名，或者根据类型构建
                    parsed_path = urlparse(texture_url).path
                    texture_filename = os.path.basename(parsed_path).split('?')[0] # 移除查询参数
                    if not texture_filename: # 如果无法解析，则error
                        raise ValueError(f"无法从URL解析出有效的纹理文件名。类型: '{texture_type}', URL: '{texture_url}'")

                    texture_file_path = os.path.join(asset_folder_path, texture_filename)
                    print(f" - Downloading {texture_type} to {texture_file_path}")
                    tex_response = requests.get(texture_url)
                    tex_response.raise_for_status()
                    with open(texture_file_path, "wb") as f:
                        f.write(tex_response.content)

        print(f"Asset bundle downloaded successfully.")
        return model_file_path


    
    def text_to_3d(self, prompt, art_style="realistic", output_dir="output", name: str = None, format="fbx"):
        preview_task_id = self._post_request("text-to-3d", {...}).get("result")
        if not self._wait_for_task_completion_stream(preview_task_id): return None
        refine_response = self._post_request("text-to-3d", {...})
        refine_task_id = refine_response.get("result")
        completed_refine_task = self._wait_for_task_completion_stream(refine_task_id)
        if not completed_refine_task: return None
        
        model_urls = completed_refine_task.get("model_urls", {})
        texture_urls = completed_refine_task.get("texture_urls", []) # 获取纹理URL列表
        model_url = model_urls.get(format)
        
        if not model_url:
            return None
        
        # 创建一个清晰的文件夹名称
        asset_folder_path = os.path.join(output_dir, name)
        
        # 调用下载函数
        return self._download_asset_bundle(asset_folder_path, model_url, texture_urls, name)