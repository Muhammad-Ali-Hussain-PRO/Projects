"""Resume/portfolio-only retrieval. Evidence is versioned with the source."""
import json
from pathlib import Path
from . import rag

ROOT=Path(__file__).resolve().parents[2]

def run(payload):
    facts=json.loads((ROOT/'data/portfolio.json').read_text())
    query=payload.get('query','What does Muhammad study?')
    return {**rag.run({'documents':facts,'query':query,'mode':payload.get('mode','local'),'top_k':4}),
            'scope':'Versioned resume and ten-project facts only; unknown claims should be declined.',
            'usage':'No external-user count claimed. Feedback is opt-in and local to the configured backend.'}
