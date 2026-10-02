#!/usr/bin/env python3
"""Derive R_STACK_ENTRY_<entry> bounds for generated C17 (Core R-FUNC-0004).

The generated translation unit is scanned for its function definitions and the direct calls,
type-glue callbacks and marked indirect-call candidates inside each body. Frame sizes come from
the Clang ``.su`` report of the same source. Every entry named by ``R_STACK_ENTRY(<name>)``
receives its own frame plus the longest callee path below it. A bound above the target's entry
budget fails the build.

The call graph may contain a cycle only when every function on it carries the mark
``/* R_STACK_RECURSION: N */`` of a function with ``@recursion(depth = N)`` (Core R-FUNC-0026):
at most N activations of such a function exist at once, and the activation that would exceed N
ends before it calls anything. A strongly connected set C of marked functions therefore needs
the sum of N(m) * frame(m) over its members, plus the larger of the largest frame in C (the
refused activation) and the deepest callee outside C.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
ENTRY_USE = re.compile(r"\bR_STACK_ENTRY\(([A-Za-z_][A-Za-z0-9_]*)\)")
INDIRECT_MARK = re.compile(r"/\* R_STACK_INDIRECT:((?: [A-Za-z_][A-Za-z0-9_]*)*) \*/")
RECURSION_MARK = re.compile(r"/\* R_STACK_RECURSION: ([1-9][0-9]*) \*/")


def strip_comments_and_strings(text: str) -> str:
    """Replace comments and literals by spaces so identifier scans see only code."""

    result: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            result.append(" " * (end - index))
            index = end
        elif text.startswith("//", index):
            end = text.find("\n", index)
            end = length if end < 0 else end
            result.append(" " * (end - index))
            index = end
        elif char in "\"'":
            end = index + 1
            while end < length and text[end] != char:
                end += 2 if text[end] == "\\" else 1
            end = min(end + 1, length)
            result.append(char + " " * max(0, end - index - 2) + (char if end - index >= 2 else ""))
            index = end
        else:
            result.append(char)
            index += 1
    return "".join(result)


def code_view(source: str) -> str:
    """Comments, literals and preprocessor lines blanked out; offsets are preserved."""

    code = strip_comments_and_strings(source)
    return "\n".join(
        (" " * len(line)) if line.lstrip().startswith("#") else line for line in code.split("\n")
    )


def parse_functions(source: str) -> dict[str, str]:
    """Return {name: body} for every top-level function definition of the generated C."""

    code = code_view(source)
    functions: dict[str, str] = {}
    depth = 0
    index = 0
    length = len(code)
    while index < length:
        char = code[index]
        if char == "{":
            depth += 1
            if depth == 1:
                name = function_name_before(code, index)
                end = matching_brace(code, index)
                if name is not None:
                    functions[name] = source[index:end]
                index = end
                depth = 0
                continue
        elif char == "}":
            depth = max(0, depth - 1)
        index += 1
    return functions


def matching_brace(code: str, start: int) -> int:
    depth = 0
    index = start
    while index < len(code):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    return len(code)


def function_name_before(code: str, brace_index: int) -> str | None:
    """A definition is `name ( parameters ) {`; anything else (struct, table) is skipped."""

    index = brace_index - 1
    while index >= 0 and code[index].isspace():
        index -= 1
    if index < 0 or code[index] != ")":
        return None
    depth = 0
    while index >= 0:
        if code[index] == ")":
            depth += 1
        elif code[index] == "(":
            depth -= 1
            if depth == 0:
                break
        index -= 1
    if index < 0:
        return None
    index -= 1
    while index >= 0 and code[index].isspace():
        index -= 1
    end = index + 1
    while index >= 0 and (code[index].isalnum() or code[index] == "_"):
        index -= 1
    name = code[index + 1 : end]
    return name if IDENTIFIER.fullmatch(name) else None


def parse_frames(report: Path, source_name: str) -> dict[str, int]:
    frames: dict[str, int] = {}
    for line in report.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            continue
        identity, size, kind = fields
        if not size.isdigit():
            continue
        location, _, name = identity.rpartition(":")
        source_path = location.split(":")[0]
        if Path(source_path).name != source_name:
            continue
        if kind.strip() != "static":
            raise SystemExit(f"non-static generated frame is not accepted: {line}")
        frames[name] = int(size)
    return frames


def body_edges(
    name: str, body: str, defined: set[str], wrappers: set[str], recursive: bool
) -> set[str]:
    edges: set[str] = set()
    for mark in INDIRECT_MARK.finditer(body):
        for target in mark.group(1).split():
            if target in defined and target != name:
                edges.add(target)
    code = strip_comments_and_strings(body)
    for match in IDENTIFIER.finditer(code):
        identifier = match.group(0)
        if identifier not in defined:
            continue
        if code[: match.start()].rstrip().endswith("(void)"):
            # `(void)name;` only keeps a definition referenced; it is never a call.
            continue
        after = code[match.end() :].lstrip()
        is_call = after.startswith("(")
        if identifier == name:
            # Only a function with @recursion may call itself (R-FUNC-0026).
            if recursive and is_call:
                edges.add(identifier)
            continue
        if is_call or identifier not in wrappers:
            # Direct calls always nest; a pointer reference nests only when the runtime may call
            # it back on this stack (type glue), never for another entry's own gate.
            edges.add(identifier)
    return edges


def strongly_connected(graph: dict[str, set[str]]) -> dict[str, int]:
    """Tarjan's components, iteratively; returns the component index of every function."""

    index: dict[str, int] = {}
    lowlink: dict[str, int] = {}
    component: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    counter = 0
    for root in sorted(graph):
        if root in index:
            continue
        work = [(root, iter(sorted(graph[root])))]
        index[root] = lowlink[root] = counter
        counter += 1
        stack.append(root)
        on_stack.add(root)
        while work:
            vertex, callees = work[-1]
            advanced = False
            for callee in callees:
                if callee not in index:
                    index[callee] = lowlink[callee] = counter
                    counter += 1
                    stack.append(callee)
                    on_stack.add(callee)
                    work.append((callee, iter(sorted(graph[callee]))))
                    advanced = True
                    break
                if callee in on_stack:
                    lowlink[vertex] = min(lowlink[vertex], index[callee])
            if advanced:
                continue
            work.pop()
            if work:
                parent = work[-1][0]
                lowlink[parent] = min(lowlink[parent], lowlink[vertex])
            if lowlink[vertex] == index[vertex]:
                identity = len(set(component.values()))
                while True:
                    member = stack.pop()
                    on_stack.discard(member)
                    component[member] = identity
                    if member == vertex:
                        break
    return component


def cycle_through(graph: dict[str, set[str]], component: dict[str, int], start: str) -> list[str]:
    """A shortest call cycle from start back to itself inside its component."""

    previous: dict[str, str] = {}
    queue = [start]
    for vertex in queue:
        for callee in sorted(graph[vertex]):
            if component[callee] != component[start]:
                continue
            if callee == start:
                path = [vertex]
                while path[-1] != start:
                    path.append(previous[path[-1]])
                return list(reversed(path)) + [start]
            if callee not in previous:
                previous[callee] = vertex
                queue.append(callee)
    return [start, start]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--budget", type=int, required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text(encoding="utf-8")
    frames = parse_frames(arguments.report, arguments.source.name)
    functions = parse_functions(source)
    defined = set(functions)
    wrappers = {name for name, body in functions.items() if ENTRY_USE.search(body)}
    entries = sorted(set(ENTRY_USE.findall(code_view(source))))
    for entry in entries:
        if entry not in defined:
            raise SystemExit(f"R_STACK_ENTRY names an undefined function: {entry}")

    depths: dict[str, int] = {}
    for name, body in functions.items():
        marks = RECURSION_MARK.findall(body)
        if len(set(marks)) > 1:
            raise SystemExit(f"function {name} carries conflicting recursion depths: {marks}")
        if marks:
            depths[name] = int(marks[0])
    # A definition without a frame was not compiled on its own: an unused static inline one, or
    # one the compiler inlined into every caller. It counts no frame, but its calls remain, so
    # its callees still count and a cycle through it still shows.
    graph = {
        name: body_edges(name, body, defined, wrappers, name in depths)
        for name, body in functions.items()
    }
    component = strongly_connected(graph)
    members: dict[int, list[str]] = {}
    for name in sorted(graph):
        members.setdefault(component[name], []).append(name)
    # Only what an entry reaches is checked, as before: unreachable glue is never on a stack.
    reachable: set[str] = set(entries)
    pending_names = list(entries)
    while pending_names:
        for callee in graph[pending_names.pop()]:
            if callee not in reachable:
                reachable.add(callee)
                pending_names.append(callee)
    cyclic: set[int] = set()
    for index, group in members.items():
        if group[0] not in reachable:
            continue
        if len(group) > 1 or group[0] in graph[group[0]]:
            unmarked = [name for name in group if name not in depths]
            if unmarked:
                cycle = " -> ".join(cycle_through(graph, component, unmarked[0]))
                raise SystemExit(f"generated call graph is not acyclic: {cycle}")
            cyclic.add(index)

    bounds: dict[int, int] = {}
    deepest: dict[int, str | None] = {}

    def bound(index: int) -> int:
        """The stack below a call into the component, deepest callee components first."""

        if index in bounds:
            return bounds[index]
        order: list[int] = []
        pending = [(index, False)]
        while pending:
            current, expanded = pending.pop()
            if current in bounds:
                continue
            if expanded:
                order.append(current)
                bounds[current] = -1
                continue
            pending.append((current, True))
            for name in members[current]:
                for callee in graph[name]:
                    target = component[callee]
                    if target != current and target not in bounds:
                        pending.append((target, False))
        for current in order:
            group = members[current]
            best = 0
            best_name: str | None = None
            for name in group:
                for callee in sorted(graph[name]):
                    target = component[callee]
                    if target == current:
                        continue
                    value = bounds[target]
                    if value > best:
                        best = value
                        best_name = callee
            if current in cyclic:
                # Every member nests at most its depth; the refused activation adds one frame.
                total = sum(depths[name] * frames.get(name, 0) for name in group)
                largest = max(frames.get(name, 0) for name in group)
                bounds[current] = total + max(best, largest)
            else:
                bounds[current] = frames.get(group[0], 0) + best
            deepest[current] = best_name
        return bounds[index]

    lines: list[str] = []
    worst = 0
    worst_entry = ""
    for entry in entries:
        value = bound(component[entry])
        lines.append(f"#define R_STACK_ENTRY_{entry} ((size_t){value})")
        if value > worst:
            worst = value
            worst_entry = entry
        if value > arguments.budget:
            chain = [entry]
            while deepest.get(component[chain[-1]]):
                chain.append(deepest[component[chain[-1]]])  # type: ignore[arg-type]
            raise SystemExit(
                f"entry {entry} needs {value} bytes of stack, above the target budget of "
                f"{arguments.budget}: {' -> '.join(chain)}"
            )
    recursive = sorted((members[index], bound(index)) for index in cyclic)
    # The bound below a call into each recursive set, named by its first member, records what
    # the entries above count for it.
    for group, value in recursive:
        counted = " ".join(f"{name}*{depths[name]}" for name in group)
        lines.append(f"#define R_STACK_RECURSION_{group[0]} ((size_t){value}) /* {counted} */")
    arguments.output.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
    print(
        f"stack entries: {len(entries)} entries, {len(functions)} functions, "
        f"worst {worst} bytes at {worst_entry or '-'}, budget {arguments.budget}"
        + "".join(
            f"; recursion {' '.join(f'{name}*{depths[name]}' for name in group)}: {value} bytes"
            for group, value in recursive
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
