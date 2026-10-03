"""Real protocol round-trip, not a JSON-RPC animation."""
import asyncio
import sys
import os
from pathlib import Path
from mcp import ClientSession
from mcp.client.stdio import stdio_client,StdioServerParameters

async def round_trip(tool='list_projects',arguments=None):
    if tool not in {'list_projects','read_context','query_sample'}: raise ValueError('Unknown MCP tool')
    params=StdioServerParameters(command=sys.executable,args=['-m','studio.mcp_server'],env={**os.environ,'PYTHONPATH':str(Path(__file__).resolve().parents[1])})
    async with stdio_client(params) as (read,write):
        async with ClientSession(read,write) as session:
            init=await session.initialize()
            tools=await session.list_tools()
            result=await session.call_tool(tool,arguments or {})
            return {'status':'error' if result.is_error else 'ok','protocol':'MCP stdio negotiated session',
                'server':init.server_info.model_dump(mode='json'),'tools':[t.name for t in tools.tools],
                'result':result.model_dump(mode='json')}

def run(payload):
    return asyncio.run(round_trip(payload.get('tool','list_projects'),payload.get('arguments',{})))
