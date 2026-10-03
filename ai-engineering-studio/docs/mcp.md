# ContextBridge MCP

An actual MCP SDK 2.3 stdio server exposes `list_projects`, `read_context(document_id)`, `query_sample(sql)` and resource `portfolio://overview`. Context IDs are allowlisted in versioned data; arbitrary local paths are refused. The query tool reuses QueryLens read-only execution over synthetic revenue records.

Run `PYTHONPATH=backend python -m studio.mcp_server`. For a real client round trip run `PYTHONPATH=backend python -c 'from studio.mcp_client import run; print(run({"tool":"list_projects"}))'` from the collection root. Client startup, initialization, discovery and calls are covered by real subprocess tests, rather than mocked JSON-RPC.

This server is local-only stdio. It is not an authenticated hosted MCP endpoint. Connect it to an MCP-compatible client using its Python command and an absolute backend path in PYTHONPATH. No API keys are needed for these read-only tools.

Reference: https://py.sdk.modelcontextprotocol.io/
