#!/usr/bin/env python3
"""Inventory every explicit incomplete semantic-lowering branch.

The frontend uses ``r_semantic_unsupported``, ``r_body_unsupported`` and one
direct diagnostic when valid R reaches a deliberately incomplete implementation
slice.  This audit enumerates every call site of the semantic translation unit,
including the ``.inc`` files it includes, instead of relying on a
hand-maintained sample.  It is a static implementation audit: a listed branch
still needs a conforming R program before it can be classified as a reproduced
acceptance defect.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from typing import Any


CALL = re.compile(r"\br_(semantic|body)_unsupported\s*\(")
DIRECT_SLICE = re.compile(r"\br_(body_diagnostic|semantic_add_diagnostic)\s*\(")
TYPE_FAILURE = re.compile(r"\br_(semantic|body)_report_type_failure\s*\(")
FUNCTION = re.compile(
    r"^(?:(?:static\s+)?(?:bool|void|size_t|uint32_t|R[A-Za-z0-9_]+)\s+)?"
    r"(r_[a-zA-Z_][a-zA-Z0-9_]*)\s*\("
)
STRING = re.compile(r'"(?:\\.|[^"\\])*"')
INCLUDE = re.compile(r'^#include "([^"]+\.inc)"$', re.MULTILINE)


def find_call_end(source: str, start: int) -> int:
    depth = 1
    index = start
    state = "code"
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "string":
            if char == "\\":
                index += 2
                continue
            if char == '"':
                state = "code"
        elif state == "character":
            if char == "\\":
                index += 2
                continue
            if char == "'":
                state = "code"
        elif state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                state = "code"
                index += 2
                continue
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "character"
        elif char == "/" and next_char == "/":
            state = "line_comment"
            index += 2
            continue
        elif char == "/" and next_char == "*":
            state = "block_comment"
            index += 2
            continue
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise ValueError("unterminated unsupported call")


def decode_string(token: str) -> str:
    return json.loads(token)


def call_messages(call: str) -> list[str]:
    tokens = STRING.findall(call)
    if not tokens:
        return []
    messages: list[str] = []
    current = ""
    previous_end = 0
    for match in STRING.finditer(call):
        between = call[previous_end : match.start()]
        if current and any(marker in between for marker in ("?", ":")):
            messages.append(current)
            current = ""
        current += decode_string(match.group(0))
        previous_end = match.end()
    if current:
        messages.append(current)
    return messages


def function_at(source: str, offset: int) -> str:
    function = "<global>"
    for line in source[:offset].splitlines():
        match = FUNCTION.match(line)
        if match is not None:
            function = match.group(1)
    return function


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


def inventory(path: Path, name: str) -> list[dict[str, Any]]:
    source = path.read_text(encoding="utf-8")
    records: list[dict[str, Any]] = []
    for match in CALL.finditer(source):
        end = find_call_end(source, match.end())
        call = source[match.start() : end + 1]
        messages = call_messages(call)
        if not messages:
            continue
        records.append(
            {
                "helper": f"r_{match.group(1)}_unsupported",
                "file": name,
                "line": source.count("\n", 0, match.start()) + 1,
                "function": function_at(source, match.start()),
                "messages": messages,
            }
        )
    for match in TYPE_FAILURE.finditer(source):
        # A type that fails to resolve without a normative diagnostic reports its last argument.
        end = find_call_end(source, match.end())
        call = source[match.start() : end + 1]
        tokens = [decode_string(token) for token in STRING.findall(call)]
        if not tokens or source[max(0, match.start() - 7) : match.start()].strip() == "bool":
            continue
        records.append(
            {
                "helper": f"r_{match.group(1)}_report_type_failure",
                "file": name,
                "line": source.count("\n", 0, match.start()) + 1,
                "function": function_at(source, match.start()),
                "messages": [tokens[-1]],
            }
        )
    for match in DIRECT_SLICE.finditer(source):
        end = find_call_end(source, match.end())
        call = source[match.start() : end + 1]
        tokens = [decode_string(token) for token in STRING.findall(call)]
        if "R-DIAG-SLICE-001" not in tokens:
            continue
        message_index = tokens.index("R-DIAG-SLICE-001") + 2
        if message_index >= len(tokens):
            continue
        records.append(
            {
                "helper": f"r_{match.group(1)}",
                "file": name,
                "line": source.count("\n", 0, match.start()) + 1,
                "function": function_at(source, match.start()),
                "messages": ["".join(tokens[message_index:])],
            }
        )
    records.sort(key=lambda record: record["line"])
    return records


def category(message: str) -> str:
    lowered = message.lower()
    for name, words in (
        ("async/thread", ("async", "thread", "task")),
        ("ownership/borrow", ("move", "ownership", "borrow", "payload")),
        ("types/aggregates", ("type", "aggregate", "enum", "value representation")),
        ("expressions/control-flow", ("expression", "operation", "assignment", "switch", "jump", "range")),
        ("module/storage", ("module", "static", "binding")),
    ):
        if any(word in lowered for word in words):
            return name
    return "other"


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path, default=root / "compiler/semantic/semantic.c"
    )
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    if not source.is_file():
        parser.error(f"semantic source does not exist: {source}")

    files = translation_unit(source)
    records = [
        record
        for path in files
        for record in inventory(path, path.relative_to(source.parent).as_posix())
    ]
    messages = [message for record in records for message in record["messages"]]
    digest = hashlib.sha256()
    for path in files:
        digest.update(path.read_bytes())
    categories = Counter(category(message) for message in messages)
    report = {
        "scope": "all explicit R-DIAG-SLICE-001 semantic call sites",
        "source": str(source),
        "files": len(files),
        "source_sha256": digest.hexdigest(),
        "call_sites": len(records),
        "messages": len(messages),
        "unique_messages": len(set(messages)),
        "categories": dict(sorted(categories.items())),
        "records": records,
    }
    output = json.dumps(report, indent=2) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(output, encoding="utf-8")
        print(
            "semantic slices: "
            f"{len(records)} call sites, {len(messages)} messages, "
            f"{len(set(messages))} unique messages"
        )
    else:
        print(output, end="")
    return 1 if records else 0


if __name__ == "__main__":
    raise SystemExit(main())
