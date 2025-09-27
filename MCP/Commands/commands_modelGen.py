# 文件名: unreal_ai_tools.py
"""AI-powered asset generation commands for the UnrealMCP bridge.

This module contains tools for generating assets using external AI services
and importing them into Unreal Engine. The process is split into two tools:
1. generate_3d_model: Generates a model from text and returns the local file path.
2. import_asset_from_path: Imports a model from a local file path into Unreal.
"""
import sys
import os
import json
from mcp.server.fastmcp import Context


# Import send_command from the parent module
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from unreal_mcp_bridge import send_command


from .meshy_handler import MeshyClient


def register_all(mcp):
    """Register all AI-related commands with the MCP server."""
    
    @mcp.tool()
    def generate_3d_model(ctx: Context, prompt: str, name: str) -> str:
        """
        Generates a 3D model from a text prompt and returns the local file path.
        
        This tool connects to Meshy AI, generates a model, waits for completion,
        and downloads the resulting .fbx file to a local directory.
        It does NOT interact with Unreal Engine.
        
        Args:
            prompt: A descriptive text prompt for the 3D model (e.g., 'a glowing magic sword').
            name: model name save to local.
        Returns:
            A JSON string containing the status and the absolute local file path of the downloaded model upon success, 
            or an error message upon failure.
            Example success: {"status": "success", "file_path": "C:/path/to/your/project/mcp_generated_assets/{name}.fbx"}
        """
        if MeshyClient is None:
            return json.dumps({"status": "error", "message": "MeshyClient is not available. Check 'meshy_handler.py'."})

        api_key = os.getenv("MESHY_API_KEY")
        if not api_key:
            return json.dumps({"status": "error", "message": "MESHY_API_KEY environment variable not set."})

        try:
            ctx.send_message(f"Initializing Meshy client for prompt: '{prompt}'...")
            client = MeshyClient(api_key=api_key)
            output_directory = "mcp_generated_assets"
            
            # 调用 Meshy AI 生成并下载模型
            model_path = client.text_to_3d(prompt=prompt, output_dir=output_directory, name=name)

            if model_path:
                absolute_model_path = os.path.abspath(model_path)
                ctx.send_message(f"Model successfully downloaded to: {absolute_model_path}")
                # 成功时，返回包含文件路径的 JSON
                return json.dumps({
                    "status": "success",
                    "file_path": absolute_model_path
                })
            else:
                return json.dumps({
                    "status": "error",
                    "message": "Failed to generate or download the 3D model from Meshy AI. Check console logs."
                })

        except Exception as e:
            return json.dumps({"status": "error", "message": f"An unexpected error occurred: {str(e)}"})

    @mcp.tool()
    def import_asset_from_path(ctx: Context, file_path: str, location: list = [0, 0, 0]) -> str:
        """
        Imports a 3D model from a local file path into the Unreal assets and add it to current scene.
        
        This tool takes a path to a model file (e.g., .glb, .fbx) and commands
        Unreal Engine to import it as an asset then put it to current scene in specified  location.
        
        Args:
            file_path: The absolute local path to the 3D model file.
            location: Optional 3D location [x, y, z] to spawn the object.
        Returns:
            A string indicating the success or failure of the this operation.
        """
        if not os.path.exists(file_path):
            return f"Error: The file path does not exist: {file_path}"
        
        try:

            ctx.send_message(f"Sending command to Unreal to import asset from: {file_path}")
            
            params = {
                "file_path": file_path,
                "location": location
            }

            response = send_command("import__asset", params)

            if response.get("status") == "success":
                asset_name = response.get('result', {}).get('name', 'Unknown Asset')
                return f"✅ Successfully imported '{asset_name}' into the scene at location {location}."
            else:
                error_message = response.get('message', 'Unknown error from Unreal.')
                return f"Error importing asset into Unreal: {error_message}"
        
        except Exception as e:
            return f"An unexpected error occurred during import: {str(e)}"