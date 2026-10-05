#!/usr/bin/env python3
"""Dependency-free checker for every JSON Schema keyword used in our contract.

This is intentionally a validator for this fixed schema subset, not a general
JSON Schema implementation. Unknown assertion keywords are rejected.
"""
import json
import math
import re
import sys
from pathlib import Path

allowed = {"$schema", "title", "description", "type", "properties", "required",
           "additionalProperties", "items", "minItems", "maxItems", "minimum",
           "maximum", "exclusiveMinimum", "minLength", "maxLength", "pattern",
           "enum", "const"}
checks = 0

def validate(value, schema, path="$", schema_only=False):
    global checks
    assert not set(schema) - allowed, (path, "unknown schema keywords", set(schema) - allowed)
    if schema_only:
        for key, child in schema.get("properties", {}).items():
            validate(None, child, path + "." + key, True)
        if "items" in schema:
            validate(None, schema["items"], path + "[]", True)
        return
    checks += 1
    types = {"object": lambda v: isinstance(v, dict),
             "array": lambda v: isinstance(v, list),
             "number": lambda v: type(v) in (int, float) and math.isfinite(v),
             "integer": lambda v: type(v) is int,
             "boolean": lambda v: type(v) is bool,
             "string": lambda v: isinstance(v, str)}
    if "type" in schema:
        assert types[schema["type"]](value), (path, "wrong type")
    if "const" in schema:
        assert type(value) is type(schema["const"]) and value == schema["const"], (path, "constant mismatch")
    if "enum" in schema:
        assert value in schema["enum"], (path, "not in enum")
    if isinstance(value, dict):
        assert set(schema.get("required", [])) <= set(value), (path, "missing properties")
        props = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            assert set(value) <= set(props), (path, "additional properties")
        for key, item in value.items():
            if key in props:
                validate(item, props[key], path + "." + key)
    elif isinstance(value, list):
        assert len(value) >= schema.get("minItems", 0), (path, "array too short")
        assert len(value) <= schema.get("maxItems", math.inf), (path, "array too long")
        for index, item in enumerate(value):
            validate(item, schema["items"], path + f"[{index}]")
    elif type(value) in (int, float):
        assert value >= schema.get("minimum", -math.inf), (path, "below minimum")
        assert value <= schema.get("maximum", math.inf), (path, "above maximum")
        if "exclusiveMinimum" in schema:
            assert value > schema["exclusiveMinimum"], (path, "below exclusive minimum")
    elif isinstance(value, str):
        assert len(value) >= schema.get("minLength", 0), (path, "string too short")
        assert len(value) <= schema.get("maxLength", math.inf), (path, "string too long")
        if "pattern" in schema:
            assert re.search(schema["pattern"], value), (path, "pattern mismatch")

schema = json.loads(Path(sys.argv[1]).read_text())
validate(None, schema, schema_only=True)
for filename in sys.argv[2:]:
    validate(json.loads(Path(filename).read_text()), schema)
print(f"PASS output schema subset: {len(sys.argv)-2} snapshots, {checks} field assertions")
