#!/usr/bin/env python3
"""Inventory every branch of MIR and C17 lowering that rejects a program as not lowerable.

MIR lowering, the C17 emitter and the C ABI bridge report ``R_FRONTEND_NOT_LOWERABLE`` when
a program reaches a form they do not lower; the command line reports it as ``R-DIAG-SLICE-001``.
Semantic analysis rejects every valid-R form outside the implemented slice before these phases
run, so each listed branch is a guard: reaching it with a program that semantic analysis
accepted is a defect (L14.2). The audit enumerates the branches of each translation unit,
including the ``.inc`` files it includes, and groups them by the phase shape they guard. It is a
static inventory; reachability is judged by running programs, not by this tool.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from typing import Any


REJECTION = re.compile(
    r"r_c17_fail\(\s*emitter,\s*R_FRONTEND_NOT_LOWERABLE\s*\)"
    r"|(?:return|=)\s+R_FRONTEND_NOT_LOWERABLE\s*;"
    r"|:\s*R_FRONTEND_NOT_LOWERABLE\s*[);]"
)
FUNCTION = re.compile(
    r"^(?:(?:static\s+)?(?:bool|void|size_t|uint32_t|int|RFrontendStatus|R[A-Za-z0-9_]+)\s+\**)?"
    r"(r_[a-zA-Z_][a-zA-Z0-9_]*)\s*\("
)
INCLUDE = re.compile(r'^#include "([^"]+\.inc)"$', re.MULTILINE)
UNITS = (
    "compiler/mir/mir.c",
    "compiler/codegen/c17.c",
    "compiler/codegen/c_abi_bridge.c",
)


def translation_unit(path: Path) -> list[Path]:
    """The source file followed by the local ``.inc`` files it includes, once each."""
    files: list[Path] = []
    pending = [path]
    while pending:
        current = pending.pop(0)
        if current in files:
            continue
        files.append(current)
        text = current.read_text(encoding="utf-8")
        for match in INCLUDE.finditer(text):
            included = current.parent / match.group(1)
            if included.is_file():
                pending.append(included)
    return files


def function_at(source: str, offset: int) -> str:
    function = "<global>"
    for line in source[:offset].splitlines():
        match = FUNCTION.match(line)
        if match is not None:
            function = match.group(1)
    return function


def category(unit: str, function: str) -> str:
    if unit.endswith("mir.c"):
        if function == "r_frontend_lower_mir":
            return "mir-lowering"
        return "mir-artifact-selection"
    if unit.endswith("c_abi_bridge.c"):
        return "c-abi-bridge"
    if "json" in function:
        return "c17-json-schema"
    if function.startswith("r_c17_preflight_async") or "_async_" in function:
        return "c17-async-frame-shape"
    if function.endswith("_call") or "_call_" in function:
        return "c17-standard-operation-shape"
    if any(word in function for word in ("type", "glue", "closure", "helper")):
        return "c17-type-support"
    if function in (
        "r_c17_preflight_expression",
        "r_c17_preflight_statement",
        "r_c17_preflight_place",
        "r_c17_preflight_aggregate",
        "r_c17_preflight_sync_initializer",
    ):
        return "c17-tree-shape"
    return "c17-program-structure"


def inventory(path: Path, unit: str, root: Path) -> list[dict[str, Any]]:
    source = path.read_text(encoding="utf-8")
    records: list[dict[str, Any]] = []
    for match in REJECTION.finditer(source):
        function = function_at(source, match.start())
        records.append(
            {
                "file": path.relative_to(root).as_posix(),
                "line": source.count("\n", 0, match.start()) + 1,
                "function": function,
                "category": category(unit, function),
            }
        )
    return records


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    root = arguments.root.resolve()

    records: list[dict[str, Any]] = []
    digest = hashlib.sha256()
    file_count = 0
    for unit in UNITS:
        source = root / unit
        if not source.is_file():
            parser.error(f"lowering source does not exist: {source}")
        for path in translation_unit(source):
            file_count += 1
            digest.update(path.read_bytes())
            records.extend(inventory(path, unit, root))
    records.sort(key=lambda record: (record["file"], record["line"]))
    categories = Counter(record["category"] for record in records)
    functions = {record["function"] for record in records}
    report = {
        "scope": "every R_FRONTEND_NOT_LOWERABLE branch of MIR and C17 lowering",
        "files": file_count,
        "source_sha256": digest.hexdigest(),
        "branches": len(records),
        "functions": len(functions),
        "categories": dict(sorted(categories.items())),
        "records": records,
    }
    output = json.dumps(report, indent=2) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(output, encoding="utf-8")
        print(
            "lowering rejections: "
            f"{len(records)} branches in {len(functions)} functions, "
            + ", ".join(f"{name} {count}" for name, count in sorted(categories.items()))
        )
    else:
        print(output, end="")
    return 1 if records else 0


if __name__ == "__main__":
    raise SystemExit(main())
