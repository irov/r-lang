#!/usr/bin/env python3
"""Generate an immutable C ABI record (schema r-abi-record-0.1) from an inventory request.

The request is the output of ``r-front --emit=abi-inventory``. For every requested record
the tool compiles the block's verified headers with the pinned C compiler in C17 mode and
derives, without executing anything on the host (R-CMAP-0024):

* the complete member inventory of every requested struct and the complete enumerator
  inventory of every requested enumeration from the compiler AST (``-ast-dump=json``);
* the record layout (size, alignment, member offsets) from ``-fdump-record-layouts-complete``;
* the compatible integer type of every enumeration from ``_Generic`` static-assertion probes.

For every requested symbol (an import whose signature passes an R-declared ``@repr(C)``
struct) the tool also records the C type of the symbol as the compiler resolved it: a type
tree of pointers, function types, arrays, scalars and struct, union and enumeration leaves,
obtained from ``__typeof__`` probes of the generator's own translation unit (data generation,
never part of the program, R-FFI-0042). Every complete struct and enumeration reached from such
a tree, directly or through members, is inventoried as well, with a type tree per member and a
layout read from integer constant expressions (``sizeof``, ``_Alignof``, ``offsetof``).

The record carries the compiler identity, its options, the feature-test definitions and
the digests of the headers it was derived from (R-FFI-0040). ``r-front`` consumes it through
``--abi-record`` to prove every complete ``@repr(C)`` aggregate declared inside an
``extern "C"`` block one-for-one (R-FFI-0017, R-FFI-0041) and every R-declared ``@repr(C)``
struct against the C type at its position in an import (R-FFI-0021, R-FFI-0041).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

REQUEST_SCHEMA = "r-abi-inventory-request-0.1"
RECORD_SCHEMA = "r-abi-record-0.1"

ENUM_CANDIDATES = (
    "int",
    "unsigned int",
    "long",
    "unsigned long",
    "long long",
    "unsigned long long",
    "short",
    "unsigned short",
    "char",
    "signed char",
    "unsigned char",
)

LAYOUT_HEADER = "*** Dumping AST Record Layout"


class RecordError(Exception):
    """A request entry cannot be turned into a record."""


def definition_line(definition: str) -> str:
    name, separator, value = definition.partition("=")
    if separator:
        return f"#define {name} {value}"
    return f"#define {name} 1"


def prelude(entry: dict[str, Any]) -> str:
    lines = [definition_line(item) for item in entry.get("feature_test_definitions", [])]
    lines.extend(f"#include <{header}>" for header in entry.get("headers", []))
    return "\n".join(lines) + "\n"


def run_compiler(
    cc: str, arguments: list[str], source: str, directory: Path, name: str
) -> subprocess.CompletedProcess[str]:
    path = directory / name
    path.write_text(source, encoding="utf-8")
    command = [cc, *arguments, str(path)]
    return subprocess.run(command, capture_output=True, text=True, check=False)


def find_header(name: str, include_dirs: list[Path]) -> Path | None:
    for directory in include_dirs:
        candidate = directory / name
        if candidate.is_file():
            return candidate
    return None


def walk(node: dict[str, Any]):
    yield node
    for child in node.get("inner", []) or []:
        if isinstance(child, dict):
            yield from walk(child)


def collect_declarations(tree: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """Index complete record definitions, enumerations and typedefs by name."""
    index: dict[str, dict[str, Any]] = {"struct": {}, "union": {}, "enum": {}, "typedef": {}}
    for node in walk(tree):
        kind = node.get("kind")
        name = node.get("name")
        if not name:
            continue
        if kind == "RecordDecl" and node.get("completeDefinition"):
            tag = node.get("tagUsed")
            if tag in ("struct", "union"):
                index[tag][name] = node
        elif kind == "EnumDecl":
            if any(child.get("kind") == "EnumConstantDecl" for child in node.get("inner", []) or []):
                index["enum"][name] = node
        elif kind == "TypedefDecl":
            index["typedef"][name] = node
    return index


def resolve_typedef(index: dict[str, dict[str, Any]], name: str) -> tuple[str, str]:
    """Follow a typedef chain to its tag kind and tag name."""
    seen: set[str] = set()
    current = name
    while current not in seen:
        seen.add(current)
        node = index["typedef"].get(current)
        if node is None:
            raise RecordError(f"typedef {name} is not declared by the headers")
        spelling = node.get("type", {}).get("desugaredQualType") or node.get("type", {}).get(
            "qualType", ""
        )
        spelling = spelling.strip()
        for tag in ("struct", "union", "enum"):
            prefix = tag + " "
            if spelling.startswith(prefix):
                return tag, spelling[len(prefix) :].strip()
        current = spelling
    raise RecordError(f"typedef {name} does not name a struct or enumeration")


def struct_members(node: dict[str, Any], c_name: str) -> list[dict[str, Any]]:
    members = []
    for child in node.get("inner", []) or []:
        kind = child.get("kind")
        if kind == "FieldDecl":
            if child.get("isBitfield"):
                raise RecordError(f"struct {c_name} has a bit-field member; not expressible")
            name = child.get("name")
            if not name:
                raise RecordError(f"struct {c_name} has an anonymous member; not expressible")
            type_info = child.get("type", {})
            member: dict[str, Any] = {"name": name, "type": type_info.get("qualType", "")}
            desugared = type_info.get("desugaredQualType")
            if desugared and desugared != member["type"]:
                member["desugared_type"] = desugared
            members.append(member)
        elif kind in ("RecordDecl", "IndirectFieldDecl"):
            raise RecordError(f"struct {c_name} has a nested anonymous aggregate; not expressible")
    return members


def enumerators(node: dict[str, Any], c_name: str) -> list[dict[str, Any]]:
    result = []
    for child in node.get("inner", []) or []:
        if child.get("kind") != "EnumConstantDecl":
            continue
        value = None
        for expression in child.get("inner", []) or []:
            if expression.get("kind") == "ConstantExpr" and "value" in expression:
                value = int(expression["value"])
                break
        if value is None:
            raise RecordError(f"enumerator {child.get('name')} of {c_name} has no evaluated value")
        result.append({"name": child["name"], "value": value})
    if not result:
        raise RecordError(f"enumeration {c_name} has no enumerators")
    return result


LAYOUT_LINE = re.compile(r"^\s*(\d+) \|(\s+)(.*)$")
LAYOUT_SIZE = re.compile(r"\[sizeof=(\d+), align=(\d+)\]")


def parse_layouts(text: str) -> dict[str, dict[str, Any]]:
    """Map a record spelling ("struct probe_pair") to its size, alignment and member offsets."""
    layouts: dict[str, dict[str, Any]] = {}
    blocks = text.split(LAYOUT_HEADER)
    for block in blocks[1:]:
        lines = [line for line in block.splitlines() if line.strip()]
        if not lines:
            continue
        head = LAYOUT_LINE.match(lines[0])
        if head is None:
            continue
        spelling = head.group(3).strip()
        base_indent = len(head.group(2))
        members: list[dict[str, Any]] = []
        size = alignment = None
        for line in lines[1:]:
            size_match = LAYOUT_SIZE.search(line)
            if size_match and "|" in line and line.split("|", 1)[0].strip() == "":
                if size is None:
                    size = int(size_match.group(1))
                    alignment = int(size_match.group(2))
                continue
            match = LAYOUT_LINE.match(line)
            if match is None:
                continue
            indent = len(match.group(2))
            if indent != base_indent + 2:
                continue
            declaration = match.group(3).strip()
            name = declaration.split()[-1].lstrip("*")
            members.append({"name": name, "offset": int(match.group(1))})
        if size is None:
            continue
        layouts[spelling] = {"size": size, "alignment": alignment, "members": members}
    return layouts


def enum_underlying_types(
    cc: str,
    arguments: list[str],
    directory: Path,
    entry_prelude: str,
    spellings: list[str],
) -> dict[str, str]:
    """Resolve the compatible integer type of each enumeration spelling with _Generic probes."""
    if not spellings:
        return {}
    lines = [entry_prelude]
    for index, spelling in enumerate(spellings):
        for candidate in ENUM_CANDIDATES:
            lines.append(
                f'_Static_assert(_Generic(({spelling})0, {candidate}: 1, default: 0), '
                f'"R_ABI_PROBE {index} {candidate}");'
            )
    probe = "\n".join(lines) + "\n"
    completed = run_compiler(
        cc, [*arguments, "-ferror-limit=0", "-fsyntax-only"], probe, directory, "enum_probe.c"
    )
    failed: dict[int, set[str]] = {}
    for match in re.finditer(r'static assertion failed: R_ABI_PROBE (\d+) ([a-z ]+)"?', completed.stderr):
        failed.setdefault(int(match.group(1)), set()).add(match.group(2).strip())
    result: dict[str, str] = {}
    for index, spelling in enumerate(spellings):
        survivors = [candidate for candidate in ENUM_CANDIDATES if candidate not in failed.get(index, set())]
        if len(survivors) != 1:
            raise RecordError(
                f"cannot determine the compatible integer type of {spelling}: {survivors}"
            )
        result[spelling] = survivors[0]
    return result


# Sugar around a type that does not change it: the named or adjusted type is the child.
TYPE_WRAPPERS = {
    "ElaboratedType",
    "ParenType",
    "AttributedType",
    "MacroQualifiedType",
    "BTFTagAttributedType",
    "CountAttributedType",
    "TypeOfExprType",
    "TypeOfType",
    "UsingType",
}


def type_children(node: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        child
        for child in node.get("inner", []) or []
        if isinstance(child, dict) and child.get("kind", "").endswith("Type")
    ]


def index_tag_declarations(tree: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """Complete record and every enumeration declaration by AST id."""
    result: dict[str, dict[str, Any]] = {}
    for node in walk(tree):
        kind = node.get("kind")
        if (kind == "RecordDecl" and node.get("completeDefinition")) or (
            kind == "EnumDecl"
            and any(child.get("kind") == "EnumConstantDecl" for child in node.get("inner", []) or [])
        ):
            result[node["id"]] = node
    return result


def normalize_type(
    node: dict[str, Any], tags: dict[str, dict[str, Any]], atomics: bool = False
) -> dict[str, Any]:
    """One compiler type node as a record type tree (schema r-abi-record-0.1). With `atomics`,
    `_Atomic(T)` is T marked atomic (the layout tables of the LLVM backend); otherwise it is not
    expressible (R-FFI-0019)."""
    kind = node.get("kind", "")
    children = type_children(node)
    spelling = node.get("type", {}).get("qualType", "")
    if kind == "QualType":
        inner = normalize_type(children[0], tags, atomics) if children else {"node": "other"}
        qualifiers = node.get("qualifiers", "").split()
        if "const" in qualifiers:
            inner["const"] = True
        if any(qualifier != "const" for qualifier in qualifiers):
            # volatile, restrict and _Atomic are not expressible (R-FFI-0019).
            inner = {"node": "other", "spelling": spelling}
        return inner
    if kind in TYPE_WRAPPERS:
        return normalize_type(children[0], tags, atomics) if children else {"node": "other", "spelling": spelling}
    if kind == "DecayedType":
        return normalize_type(children[-1], tags, atomics) if children else {"node": "other", "spelling": spelling}
    if kind == "TypedefType":
        inner = normalize_type(children[0], tags, atomics) if children else {"node": "other", "spelling": spelling}
        name = node.get("decl", {}).get("name", "")
        if name and inner.get("node") in ("record", "enum"):
            # The outermost typedef is how the header spelled this position.
            inner["typedef"] = name
        return inner
    if kind == "BuiltinType":
        if spelling == "void":
            return {"node": "void"}
        return {"node": "scalar", "spelling": spelling}
    if kind == "PointerType":
        return {"node": "pointer", "pointee": normalize_type(children[0], tags, atomics)}
    if kind == "FunctionProtoType":
        return {
            "node": "function",
            "result": normalize_type(children[0], tags, atomics),
            "parameters": [normalize_type(child, tags, atomics) for child in children[1:]],
            "variadic": bool(node.get("variadic", False)),
        }
    if kind == "FunctionNoProtoType":
        return {"node": "other", "spelling": spelling}
    if kind == "RecordType":
        decl = node.get("decl", {})
        declaration = tags.get(decl.get("id", ""))
        tag = declaration.get("tagUsed", "struct") if declaration else (
            "union" if spelling.startswith("union ") else "struct"
        )
        # The declaration of this compilation only; AST ids never leave the generator.
        return {
            "node": "record",
            "tag": tag,
            "tag_name": decl.get("name", "") or "",
            "complete": declaration is not None,
            "_declaration": declaration,
        }
    if kind == "EnumType":
        decl = node.get("decl", {})
        return {
            "node": "enum",
            "tag_name": decl.get("name", "") or "",
            "_declaration": tags.get(decl.get("id", "")),
        }
    if kind == "AtomicType" and atomics and children:
        inner = dict(normalize_type(children[0], tags, atomics))
        inner["atomic"] = True
        return inner
    if kind == "ConstantArrayType":
        return {"node": "array", "length": int(node.get("size", 0)), "element": normalize_type(children[0], tags, atomics)}
    return {"node": "other", "spelling": spelling}


def tree_leaves(tree: dict[str, Any]):
    """Struct, union and enumeration leaves of a type tree."""
    node = tree.get("node")
    if node in ("record", "enum"):
        yield tree
    elif node == "pointer":
        yield from tree_leaves(tree["pointee"])
    elif node == "array":
        yield from tree_leaves(tree["element"])
    elif node == "function":
        yield from tree_leaves(tree["result"])
        for parameter in tree["parameters"]:
            yield from tree_leaves(parameter)


def leaf_key(leaf: dict[str, Any]) -> tuple[str, str] | None:
    """The inventory entry of a leaf: its tag, else the typedef naming an anonymous tag."""
    tag_kind = "enum" if leaf["node"] == "enum" else leaf.get("tag", "struct")
    if leaf.get("tag_name"):
        return (leaf["tag_name"], tag_kind)
    if leaf.get("typedef"):
        return (leaf["typedef"], "typedef")
    return None


def leaf_spelling(key: tuple[str, str]) -> str:
    name, kind = key
    return name if kind == "typedef" else f"{kind} {name}"


def compile_probe(cc: str, arguments: list[str], source: str, directory: Path, name: str) -> dict[str, Any]:
    completed = run_compiler(
        cc, [*arguments, "-fsyntax-only", "-Xclang", "-ast-dump=json"], source, directory, name
    )
    if completed.returncode != 0:
        raise RecordError(f"type probe does not compile:\n{completed.stderr}")
    return json.loads(completed.stdout)


def typedef_trees(tree: dict[str, Any], prefix: str, tags: dict[str, dict[str, Any]]) -> dict[str, dict[str, Any]]:
    result = {}
    for node in tree.get("inner", []) or []:
        name = node.get("name", "")
        if node.get("kind") == "TypedefDecl" and name.startswith(prefix):
            children = type_children(node)
            if not children:
                raise RecordError(f"type probe {name} has no type")
            result[name] = normalize_type(children[0], tags)
    return result


def constant_values(tree: dict[str, Any], prefix: str) -> dict[str, int]:
    result = {}
    for node in walk(tree):
        name = node.get("name", "")
        if node.get("kind") == "EnumConstantDecl" and name.startswith(prefix):
            for expression in node.get("inner", []) or []:
                if expression.get("kind") == "ConstantExpr" and "value" in expression:
                    result[name] = int(expression["value"])
    return result


def strip_private(value: Any) -> Any:
    """A type tree without the generator's own keys."""
    if isinstance(value, dict):
        return {key: strip_private(item) for key, item in value.items() if not key.startswith("_")}
    if isinstance(value, list):
        return [strip_private(item) for item in value]
    return value


def symbol_trees(
    entry: dict[str, Any], cc: str, arguments: list[str], directory: Path, entry_prelude: str
) -> list[dict[str, Any]]:
    """The C type of every requested symbol as the compiler resolved it."""
    requested = entry.get("symbols", [])
    lines = [entry_prelude]
    for index, symbol in enumerate(requested):
        lines.append(f"typedef __typeof__({symbol['c_name']}) r_abi_symbol_{index};")
    tree = compile_probe(cc, arguments, "\n".join(lines) + "\n", directory, "symbol_probe.c")
    tags = index_tag_declarations(tree)
    trees = typedef_trees(tree, "r_abi_symbol_", tags)
    symbols = []
    for index, symbol in enumerate(requested):
        symbol_tree = trees.get(f"r_abi_symbol_{index}")
        if symbol_tree is None:
            raise RecordError(f"symbol {symbol['c_name']} is not declared by the headers")
        kind = symbol.get("kind", "function")
        if (kind == "function") != (symbol_tree.get("node") == "function"):
            raise RecordError(f"symbol {symbol['c_name']} is not a {kind}")
        symbols.append({"c_name": symbol["c_name"], "kind": kind, "type": symbol_tree})
    return symbols


def reached_types(
    symbols: list[dict[str, Any]],
    cc: str,
    arguments: list[str],
    directory: Path,
    entry_prelude: str,
) -> list[dict[str, Any]]:
    """Inventories of every struct and enumeration reached from the symbol trees, transitively
    through struct members; a member of each struct carries its own type tree."""
    pending = [leaf for symbol in symbols for leaf in tree_leaves(symbol["type"])]
    seen: set[tuple[str, str]] = set()
    inventories: list[dict[str, Any]] = []
    round_index = 0
    while pending:
        batch: list[tuple[tuple[str, str], dict[str, Any]]] = []
        for leaf in pending:
            key = leaf_key(leaf)
            if key is None or key in seen:
                continue
            seen.add(key)
            batch.append((key, leaf))
        pending = []
        if not batch:
            break
        records = []
        lines = [entry_prelude, "#include <stddef.h>"]
        for position, (key, leaf) in enumerate(batch):
            spelling = leaf_spelling(key)
            inventory: dict[str, Any] = {"c_name": key[0], "kind": key[1]}
            declaration = leaf.get("_declaration")
            if key[1] == "typedef":
                inventory["tag"] = "enum" if leaf["node"] == "enum" else leaf.get("tag", "struct")
                inventory["tag_name"] = leaf.get("tag_name", "")
            inventories.append(inventory)
            if leaf["node"] == "enum":
                if declaration is None:
                    inventory["inexpressible"] = f"{spelling} has no enumerators"
                    continue
                try:
                    inventory["enumerators"] = enumerators(declaration, spelling)
                except RecordError as failure:
                    inventory["inexpressible"] = str(failure)
                    continue
                records.append((position, inventory, spelling, None))
                continue
            if leaf.get("tag") != "struct":
                inventory["inexpressible"] = "complete unions require wrapper functions"
                continue
            if declaration is None:
                inventory["inexpressible"] = f"{spelling} is incomplete"
                continue
            try:
                members = struct_members(declaration, spelling)
            except RecordError as failure:
                inventory["inexpressible"] = str(failure)
                continue
            inventory["members"] = members
            records.append((position, inventory, spelling, members))
            lines.append(
                f"enum {{ r_abi_size_{round_index}_{position} = (int)sizeof({spelling}), "
                f"r_abi_alignment_{round_index}_{position} = (int)_Alignof({spelling}) }};"
            )
            for member_index, member in enumerate(members):
                lines.append(
                    f"enum {{ r_abi_offset_{round_index}_{position}_{member_index} = "
                    f"(int)offsetof({spelling}, {member['name']}) }};"
                )
                lines.append(
                    f"typedef __typeof__((({spelling} *)0)->{member['name']}) "
                    f"r_abi_member_{round_index}_{position}_{member_index};"
                )
        enum_spellings = [spelling for _, _, spelling, members in records if members is None]
        if len(lines) > 2:
            tree = compile_probe(
                cc, arguments, "\n".join(lines) + "\n", directory, f"member_probe_{round_index}.c"
            )
            trees = typedef_trees(
                tree, f"r_abi_member_{round_index}_", index_tag_declarations(tree)
            )
            values = constant_values(tree, "r_abi_")
            for position, inventory, spelling, members in records:
                if members is None:
                    continue
                inventory["size"] = values[f"r_abi_size_{round_index}_{position}"]
                inventory["alignment"] = values[f"r_abi_alignment_{round_index}_{position}"]
                for member_index, member in enumerate(members):
                    member["offset"] = values[f"r_abi_offset_{round_index}_{position}_{member_index}"]
                    member_tree = trees[f"r_abi_member_{round_index}_{position}_{member_index}"]
                    member["type_tree"] = member_tree
                    pending.extend(tree_leaves(member_tree))
        if enum_spellings:
            underlying = enum_underlying_types(cc, arguments, directory, entry_prelude, enum_spellings)
            for _, inventory, spelling, members in records:
                if members is None:
                    inventory["underlying"] = underlying[spelling]
        round_index += 1
    return inventories


def build_record(
    entry: dict[str, Any],
    cc: str,
    base_arguments: list[str],
    include_dirs: list[Path],
    directory: Path,
    compiler_identity: str,
    target_triple: str,
) -> dict[str, Any]:
    entry_prelude = prelude(entry)
    completed = run_compiler(
        cc,
        [*base_arguments, "-fsyntax-only", "-Xclang", "-ast-dump=json"],
        entry_prelude,
        directory,
        "inventory.c",
    )
    if completed.returncode != 0:
        raise RecordError(f"headers do not compile:\n{completed.stderr}")
    tree = json.loads(completed.stdout)
    index = collect_declarations(tree)
    types: list[dict[str, Any]] = []
    struct_instances: list[tuple[str, str]] = []
    enum_spellings: list[str] = []
    for requested in entry.get("types", []):
        c_name = requested["c_name"]
        kind = requested["kind"]
        record: dict[str, Any] = {"c_name": c_name, "kind": kind}
        tag_kind, tag_name = kind, c_name
        if kind == "typedef":
            tag_kind, tag_name = resolve_typedef(index, c_name)
            record["tag"] = tag_kind
            record["tag_name"] = tag_name
            spelling = c_name
        else:
            spelling = f"{tag_kind} {tag_name}"
        if tag_kind == "struct":
            node = index["struct"].get(tag_name)
            if node is None:
                raise RecordError(f"struct {tag_name} is not completely declared by the headers")
            record["members"] = struct_members(node, tag_name)
            struct_instances.append((spelling, f"struct {tag_name}"))
        elif tag_kind == "enum":
            node = index["enum"].get(tag_name)
            if node is None:
                raise RecordError(f"enumeration {tag_name} is not declared by the headers")
            record["enumerators"] = enumerators(node, tag_name)
            enum_spellings.append(spelling)
        else:
            raise RecordError(f"{tag_kind} {tag_name}: complete unions require wrapper functions")
        types.append(record)
    if struct_instances:
        # The complete dump covers every complete record of the headers, nested ones included.
        completed = run_compiler(
            cc,
            [*base_arguments, "-fsyntax-only", "-Xclang", "-fdump-record-layouts-complete"],
            entry_prelude,
            directory,
            "layouts.c",
        )
        if completed.returncode != 0:
            raise RecordError(f"record layout dump failed:\n{completed.stderr}")
        layouts = parse_layouts(completed.stdout + completed.stderr)
        for record, (_, tag_spelling) in zip(
            (item for item in types if "members" in item), struct_instances
        ):
            layout = layouts.get(tag_spelling)
            if layout is None:
                raise RecordError(f"no record layout for {tag_spelling}")
            offsets = {member["name"]: member["offset"] for member in layout["members"]}
            if len(offsets) != len(record["members"]):
                raise RecordError(f"layout member count disagrees for {tag_spelling}")
            for member in record["members"]:
                if member["name"] not in offsets:
                    raise RecordError(f"no offset for member {member['name']} of {tag_spelling}")
                member["offset"] = offsets[member["name"]]
            record["size"] = layout["size"]
            record["alignment"] = layout["alignment"]
    underlying = enum_underlying_types(cc, base_arguments, directory, entry_prelude, enum_spellings)
    if enum_spellings:
        source = entry_prelude + "".join(
            f"{spelling} r_abi_enum_instance_{index};\n" for index, spelling in enumerate(enum_spellings)
        )
        # sizeof/_Alignof of an enumeration equal those of its compatible integer type.
        probe_index = 0
        for record in types:
            if "enumerators" not in record:
                continue
            spelling = enum_spellings[probe_index]
            probe_index += 1
            record["underlying"] = underlying[spelling]
    symbols: list[dict[str, Any]] = []
    if entry.get("symbols"):
        symbols = symbol_trees(entry, cc, base_arguments, directory, entry_prelude)
        requested_keys = {(item["c_name"], item["kind"]): item for item in types}
        for inventory in reached_types(symbols, cc, base_arguments, directory, entry_prelude):
            key = (inventory["c_name"], inventory["kind"])
            existing = requested_keys.get(key)
            if existing is None:
                types.append(inventory)
                requested_keys[key] = inventory
            elif "members" in existing and "members" in inventory:
                # A requested inventory keeps its layout and gains the member type trees.
                for member, reached in zip(existing["members"], inventory["members"]):
                    if member["name"] == reached["name"] and "type_tree" in reached:
                        member["type_tree"] = reached["type_tree"]
        symbols = strip_private(symbols)
        types = strip_private(types)
    headers = []
    for header in entry.get("headers", []):
        path = find_header(header, include_dirs)
        if path is None:
            raise RecordError(f"header {header} is not under any include directory")
        headers.append({"spelling": header, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    record = {
        "name": entry["name"],
        "provider": entry.get("provider", ""),
        "compiler_identity": compiler_identity,
        "options": base_arguments,
        "feature_test_definitions": list(entry.get("feature_test_definitions", [])),
        "headers": headers,
        "types": types,
    }
    if symbols:
        record["symbols"] = symbols
    if target_triple:
        # Only a request that names its target pins the record to it (R-FFI-0040).
        record["target_triple"] = target_triple
    return record


def merge_entries(entries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Blocks that name one record share it: union of headers, definitions and types."""
    merged: dict[str, dict[str, Any]] = {}
    for entry in entries:
        name = entry["name"]
        target = merged.setdefault(
            name,
            {
                "name": name,
                "provider": entry.get("provider", ""),
                "feature_test_definitions": [],
                "headers": [],
                "types": [],
                "symbols": [],
            },
        )
        for definition in entry.get("feature_test_definitions", []):
            if definition not in target["feature_test_definitions"]:
                target["feature_test_definitions"].append(definition)
        for header in entry.get("headers", []):
            if header not in target["headers"]:
                target["headers"].append(header)
        for requested in entry.get("types", []):
            if requested not in target["types"]:
                target["types"].append(requested)
        for requested in entry.get("symbols", []):
            if requested not in target["symbols"]:
                target["symbols"].append(requested)
    return [merged[name] for name in sorted(merged)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("-I", dest="include_dirs", action="append", default=[], type=Path)
    parser.add_argument("--sysroot", type=Path)
    parser.add_argument("--std", default="c17")
    args = parser.parse_args()

    request = json.loads(args.request.read_text(encoding="utf-8"))
    if request.get("schema") != REQUEST_SCHEMA:
        print(f"unexpected request schema: {request.get('schema')}", file=sys.stderr)
        return 2
    base_arguments = [f"-std={args.std}", "-pedantic-errors", "-Wall", "-Wextra", "-Werror"]
    if args.sysroot is not None:
        base_arguments.extend(["-isysroot", str(args.sysroot)])
    for directory in args.include_dirs:
        base_arguments.append(f"-I{directory}")
    version = subprocess.run([args.cc, "--version"], capture_output=True, text=True, check=False)
    compiler_identity = (version.stdout.splitlines() or ["unknown"])[0].strip()
    # R-FFI-0044: the identity names the toolchain build, which for a clang release is the
    # digest of the compiler executable (tools/check_target_toolchain.py).
    located = shutil.which(args.cc)
    if located is not None:
        executable = Path(os.path.realpath(located))
        digest = hashlib.sha256(executable.read_bytes()).hexdigest()
        compiler_identity = f"{compiler_identity} (sha256:{digest})"
    records = []
    with tempfile.TemporaryDirectory(prefix="r-abi-record-") as scratch:
        directory = Path(scratch)
        for entry in merge_entries(request.get("records", [])):
            try:
                records.append(
                    build_record(
                        entry,
                        args.cc,
                        base_arguments,
                        args.include_dirs,
                        directory,
                        compiler_identity,
                        request.get("target_triple", ""),
                    )
                )
            except RecordError as failure:
                print(f"abi record {entry.get('name')}: {failure}", file=sys.stderr)
                return 1
    document = {"schema": RECORD_SCHEMA, "records": records}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
