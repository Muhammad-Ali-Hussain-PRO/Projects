#!/usr/bin/env python3
"""CLI integration checks: output contract, replay, actions, and safe rejection."""
import json
import subprocess
import sys

binary = sys.argv[1]
checks = 0

def run(*args, code=0):
    global checks
    p = subprocess.run([binary, *args], text=True, capture_output=True, check=False)
    assert p.returncode == code, (args, p.returncode, p.stderr)
    checks += 1
    return p

a = run("--seed", "42", "--steps", "600", "--json")
b = run("--seed", "42", "--steps", "600", "--json")
assert a.stdout == b.stdout
data = json.loads(a.stdout)
assert data["project"] == "aether" and data["mode"] == "native"
assert data["frames"][0]["step"] == 0 and data["frames"][-1]["step"] == 600
assert len(data["frames"]) == 21 and data["summary"]["validation"]["ok"]
assert all(len(g["vertices"]) % 3 == 0 and len(g["indices"]) % 3 == 0 for g in data["geometry"])
assert all(all(i < len(g["vertices"]) // 3 for i in g["indices"]) for g in data["geometry"])
checks += 7
scheduled = json.loads(run("--steps", "2400", "--action", "120:gate=on", "--json").stdout)
assert not scheduled["frames"][0]["valves"][1]["open"]
assert scheduled["frames"][-1]["valves"][1]["open"]
assert scheduled["frames"][-1]["puzzle_solved"]
checks += 3
zero = json.loads(run("--steps", "0", "--valve", "gate=on", "--json").stdout)
assert len(zero["frames"]) == 1 and zero["frames"][0]["valves"][1]["open"]
checks += 1
for args in [
    ("--steps", "-1"), ("--steps", "100001"), ("--seed", "4294967296"),
    ("--sample-every", "0"), ("--prompt", "island; eval javascript"),
    ("--action", "999:gate=on", "--steps", "10"),
    ("--prompt", "water off", "--valve", "gate=on"),
    ("--unknown", "value"),
]:
    run(*args, code=2)
print(f"PASS CLI integration: {checks} checks")
