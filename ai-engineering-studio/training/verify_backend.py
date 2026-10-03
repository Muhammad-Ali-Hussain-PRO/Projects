"""Run the full real backend suite and write machine-readable case evidence."""
from collections import Counter, defaultdict
from datetime import datetime, timezone
import importlib.metadata
import io
import json
from pathlib import Path
import platform
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "backend"))


class EvidenceResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.evidence = []
        self.started = {}

    def startTest(self, test):
        self.started[test.id()] = time.perf_counter()
        super().startTest(test)

    def record(self, test, outcome, detail=None):
        row = {"test": test.id(), "file": "tests/" + test.id().split(".")[0] + ".py", "outcome": outcome,
               "duration_ms": round((time.perf_counter() - self.started.get(test.id(), time.perf_counter())) * 1000, 3)}
        if detail:
            row["detail"] = detail
        self.evidence.append(row)

    def addSuccess(self, test):
        self.record(test, "passed")
        super().addSuccess(test)

    def addFailure(self, test, err):
        self.record(test, "failed", self._exc_info_to_string(err, test))
        super().addFailure(test, err)

    def addError(self, test, err):
        self.record(test, "error", self._exc_info_to_string(err, test))
        super().addError(test, err)

    def addSkip(self, test, reason):
        self.record(test, "skipped", reason)
        super().addSkip(test, reason)

    def addSubTest(self, test, subtest, err):
        if err is not None:
            self.record(subtest, "failed_subtest", self._exc_info_to_string(err, test))
        super().addSubTest(test, subtest, err)


def main():
    suite = unittest.defaultTestLoader.discover(str(ROOT / "tests"))
    log = io.StringIO()
    started = time.perf_counter()
    result = unittest.TextTestRunner(stream=log, verbosity=2, resultclass=EvidenceResult).run(suite)
    file_counts = defaultdict(Counter)
    for case in result.evidence:
        file_counts[case["file"]][case["outcome"]] += 1
    versions = {}
    for package in ["fastapi", "starlette", "httpx", "mcp", "numpy", "scikit-learn", "pypdf", "Pillow", "jsonschema"]:
        try:
            versions[package] = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError:
            versions[package] = None
    report = {"status": "passed" if result.wasSuccessful() else "failed",
              "generated_at": datetime.now(timezone.utc).isoformat(), "command": "python training/verify_backend.py",
              "python": platform.python_version(), "dependencies": versions,
              "total_tests": result.testsRun, "failures": len(result.failures), "errors": len(result.errors), "skipped": len(result.skipped),
              "duration_seconds": round(time.perf_counter() - started, 3),
              "per_file": {path: dict(counts) for path, counts in sorted(file_counts.items())}, "cases": result.evidence,
              "scope": "Actual local unit/integration execution, including real MCP subprocess sessions. Provider transport tests use explicit mocks; no live account calls."}
    destination = ROOT / "artifacts/verification"
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "backend_tests.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (destination / "backend_tests.log").write_text(log.getvalue(), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ["status", "total_tests", "failures", "errors", "skipped", "duration_seconds", "per_file"]}, indent=2))
    if not result.wasSuccessful():
        print(log.getvalue(), file=sys.stderr)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
