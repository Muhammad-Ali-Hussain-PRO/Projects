"""Real subprocess stdio sessions: no protocol mocks or third-party targets."""
import asyncio
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))
from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from studio import mcp_client


def text_payload(result):
    return json.loads(next(block.text for block in result.content if getattr(block, "type", None) == "text"))


class MCPTests(unittest.IsolatedAsyncioTestCase):
    async def test_real_stdio_initialize_tools_resources_and_restricted_queries(self):
        params = StdioServerParameters(command=sys.executable, args=["-m", "studio.mcp_server"],
                                       env={**os.environ, "PYTHONPATH": str(ROOT / "backend")})
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                initialized = await asyncio.wait_for(session.initialize(), timeout=15)
                self.assertEqual(initialized.server_info.name, "Hussain Engineering Context")
                tools = await session.list_tools()
                self.assertEqual({tool.name for tool in tools.tools}, {"list_projects", "read_context", "query_sample"})
                resources = await session.list_resources()
                self.assertIn("portfolio://overview", {str(resource.uri) for resource in resources.resources})
                projects = await session.call_tool("list_projects", {})
                self.assertFalse(projects.is_error)
                self.assertEqual(len(projects.structured_content["result"]), 10)
                education = await session.call_tool("read_context", {"document_id": "education"})
                self.assertFalse(education.is_error)
                self.assertEqual(text_payload(education)["id"], "education")
                rejected = await session.call_tool("read_context", {"document_id": "../../etc/passwd"})
                self.assertTrue(rejected.is_error)
                query = await session.call_tool("query_sample", {"sql": "SELECT SUM(revenue) AS total FROM data"})
                self.assertFalse(query.is_error)
                self.assertEqual(text_payload(query)["rows"], [[6900.0]])
                mutation = await session.call_tool("query_sample", {"sql": "DELETE FROM data"})
                self.assertTrue(mutation.is_error)
                filesystem = await session.call_tool("query_sample", {"sql": "SELECT load_extension('/tmp/anything')"})
                self.assertTrue(filesystem.is_error)
                overview = await session.read_resource("portfolio://overview")
                context = json.loads(overview.contents[0].text)
                self.assertIn("education", {item["id"] for item in context})

    async def test_client_adapter_negotiates_actual_round_trip(self):
        result = await asyncio.wait_for(mcp_client.round_trip("query_sample", {"sql": "SELECT COUNT(*) AS rows FROM data"}), timeout=15)
        self.assertEqual(result["status"], "ok")
        self.assertEqual(set(result["tools"]), {"list_projects", "read_context", "query_sample"})
        self.assertIn("negotiated", result["protocol"])
        content = next(block for block in result["result"]["content"] if block["type"] == "text")
        self.assertEqual(json.loads(content["text"])["rows"], [[6]])

    async def test_client_rejects_non_allowlisted_tool_before_spawning(self):
        with self.assertRaises(ValueError):
            await mcp_client.round_trip("read_file", {"path": "/etc/passwd"})


if __name__ == "__main__":
    unittest.main()
