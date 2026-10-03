"""Run only the local synthetic guardrail corpus; no provider requests."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))
from studio.safety import benchmark


if __name__ == "__main__":
    report = benchmark()
    output = ROOT / "artifacts/safety/local_report.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"case_counts": report["case_counts"], "injection_metrics": report["injection_metrics"], "pii_metrics": report["pii_metrics"]}, indent=2))
