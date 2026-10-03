"""Actual MCP 2.x stdio server with allowlisted, read-only tools."""
import json
from pathlib import Path
from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from . import analyst

ROOT=Path(__file__).resolve().parents[2]
server=MCPServer('Hussain Engineering Context',instructions='Read-only portfolio and synthetic analytics. No filesystem traversal or arbitrary tools.')

@server.tool()
def list_projects() -> list[dict]:
    """List the ten AI Studio projects and their verified implementation status."""
    return json.loads((ROOT/'data/catalog.json').read_text())

@server.tool()
def read_context(document_id: str) -> dict:
    """Read a versioned portfolio fact by its allowlisted document ID."""
    for doc in json.loads((ROOT/'data/portfolio.json').read_text()):
        if doc['id']==document_id: return doc
    raise ToolError('Unknown context ID; paths and URLs are not accepted.')

@server.tool()
def query_sample(sql: str) -> dict:
    """Execute a bounded read-only SELECT against the synthetic revenue table named data."""
    try: return analyst.execute(analyst.SAMPLE,sql)
    except ValueError as exc: raise ToolError('Query rejected by the bounded read-only SQL validator.') from exc

@server.resource('portfolio://overview')
def overview() -> str:
    return (ROOT/'data/portfolio.json').read_text()

if __name__=='__main__':
    server.run(transport='stdio')
