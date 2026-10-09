#!/usr/bin/env python3
"""Compile every concrete public library name from R source.

The generated implementation inventory primarily validates files, symbols and
metadata.  This audit checks the independent source-language boundary: every
concrete public operation must be recognized by the compiler before argument
checking.  A zero-argument call is deliberate.  A resolved operation may reject
that call for arity or type reasons; an unresolved-name diagnostic proves that
the advertised operation cannot be called from R at all.  The compiler's
explicit no-lowering diagnostic is tracked separately so a name-only registry
cannot turn an unimplemented operation green.

Closed suffix families are expanded here because their inventory record names
are schemas rather than concrete source spellings.  Public types are carried to
LLVM IR through a nullable raw pointer (every function lowered, --all-functions)
so the check also detects names that HIR accepts as arbitrary opaque standard
types but the backend cannot represent.
This audit does not prove that a resolved operation accepts its valid signature
or that a valid call has working runtime behavior.  Those contracts require the
module runtime tests and code-generation fixtures.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Any


EXTRA_COMPILER_ARGUMENTS: list[str] = []
UNRESOLVED_MESSAGE = "called function name is unresolved at this source position"

# Library R-SLIB-RSRC-0001: an R-source module is imported before its items are named; the
# probe of every such item starts with the import of its module.
R_SOURCE_MODULES: dict[str, str] = {}

# R-source generic types whose parameters need arguments beyond a scalar: an iterator
# parameter takes the range source, and a closure parameter can only come from a lambda.
R_SOURCE_TYPE_ARGUMENTS: dict[str, list[str]] = {
    "std.iter::take_iter": ["std.iter::range_i32"],
    "std.iter::skip_iter": ["std.iter::range_i32"],
    "std.iter::enumerate_iter": ["std.iter::range_i32"],
    "std.iter::chain_iter": ["std.iter::range_i32"],
    "std.iter::zip_iter": ["std.iter::range_i32", "std.iter::range_i32"],
    # M18: buffered readers and writers and duplex streams take stream types.
    "std.bufio::reader": ["std.io::input"],
    "std.bufio::writer": ["std.io::output"],
    "std.stream::duplex": ["std.io::input", "std.io::output"],
    "std.tls::stream": ["std.net::tcp_stream"],
}
R_SOURCE_TYPE_PROBES: dict[str, str] = {
    "std.iter::map_iter": (
        "void probe() { fn i32 twice(i32 x) { return x * 2; } "
        "std.iter::range_i32 source = std.iter::range(0, 2); "
        "auto value = std.iter::map(move source, &twice); (move value) as void; }"
    ),
    "std.iter::filter_map_iter": (
        "void probe() { fn o<i32> keep(i32 x) { return o::some(x); } "
        "std.iter::range_i32 source = std.iter::range(0, 2); "
        "auto value = std.iter::filter_map(move source, &keep); (move value) as void; }"
    ),
    # M19: the filter adapter takes a predicate over borrowed items.
    "std.iter::filter_iter": (
        "void probe() { fn bool keep(const i32* x) { return *x > 0; } "
        "std.iter::range_i32 source = std.iter::range(0, 2); "
        "auto value = std.iter::filter(move source, &keep); (move value) as void; }"
    ),
}


def r_source_import(name: str) -> str:
    module = name.split("::", maxsplit=1)[0].split("(", maxsplit=1)[0]
    return f"import {module};\n" if module in R_SOURCE_MODULES else ""
UNIMPLEMENTED_MESSAGE = "public standard library operation has no source-level lowering"

AMBIGUOUS_OPERATION_PROBES = {
    "std.array::create": (
        "void probe() { array<i32> value = std.array::create::<i32>(); drop value; }"
    ),
    "std.array::with_capacity": (
        "void probe() throws std.alloc::alloc_error { "
        "array<i32> value = std.array::with_capacity::<i32>(1usize); drop value; }"
    ),
    "std.dict::create": (
        "void probe() { dict<i32,i32> value = std.dict::create::<i32, i32>(); drop value; }"
    ),
    "std.dict::iter": (
        "void probe() { dict<i32,i32> value = std.dict::create::<i32, i32>(); "
        "std.dict::iter<i32,i32> cursor = std.dict::iter(&value); drop cursor; }"
    ),
    "std.dict::with_capacity": (
        "void probe() throws std.alloc::alloc_error { "
        "dict<i32,i32> value = std.dict::with_capacity::<i32, i32>(1usize); drop value; }"
    ),
    "std.list::create": (
        "void probe() { list<i32> value = std.list::create::<i32>(); drop value; }"
    ),
    "std.list::iter": (
        "void probe() { list<i32> value = std.list::create::<i32>(); "
        "std.list::iter<i32> cursor = std.list::iter(&value); drop cursor; }"
    ),
    "std.sync::channel": (
        "void probe() throws std.alloc::alloc_error { "
        "std.sync::channel<i32> value = std.sync::channel::<i32>(); drop value; }"
    ),
    "std.sync::once_lock": (
        "void probe() { std.sync::once_lock<i32> value = std.sync::once_lock::<i32>(); "
        "drop value; }"
    ),
    "std.sync::receiver": (
        "std.sync::receiver<i32> probe(std.sync::channel<i32> value) { "
        "return std.sync::receiver(move value); }"
    ),
    "std.sync::sender": (
        "std.sync::sender<i32> probe(const std.sync::channel<i32>* value) { "
        "return std.sync::sender(value); }"
    ),
    "std.sync::sync_channel": (
        "void probe() throws std.alloc::alloc_error { "
        "std.sync::sync_channel<i32> value = std.sync::sync_channel::<i32>(1usize); "
        "drop value; }"
    ),
    "std.sync::sync_sender": (
        "std.sync::sync_sender<i32> probe(const std.sync::sync_channel<i32>* value) { "
        "return std.sync::sync_sender(value); }"
    ),
}

R_INTEGER_SUFFIXES = (
    "i8",
    "u8",
    "i16",
    "u16",
    "i32",
    "u32",
    "i64",
    "u64",
    "isize",
    "usize",
)

NUMERIC_SUFFIXES = R_INTEGER_SUFFIXES + (
    "f32",
    "f64",
    "c_char",
    "c_schar",
    "c_uchar",
    "c_short",
    "c_ushort",
    "c_int",
    "c_uint",
    "c_long",
    "c_ulong",
    "c_llong",
    "c_ullong",
    "c_bool",
    "c_wchar",
    "c_wint",
    "c_int8",
    "c_uint8",
    "c_int16",
    "c_uint16",
    "c_int32",
    "c_uint32",
    "c_int64",
    "c_uint64",
    "c_intptr",
    "c_uintptr",
    "c_intmax",
    "c_uintmax",
    "c_float",
    "c_double",
    "c_long_double",
    "c_size",
    "c_ptrdiff",
)

C_NUMERIC_SUFFIXES = NUMERIC_SUFFIXES[12:]
FORMAT_INTEGER_SUFFIXES = tuple(
    suffix
    for suffix in NUMERIC_SUFFIXES
    if suffix not in {"f32", "f64", "c_float", "c_double", "c_long_double"}
)

STANDARD_PARAMETRIC_TYPES = {
    "core::atomic_compare_exchange_result": 1,
    "std.alloc::new_error": 1,
    "std.arc::try_unwrap_result": 1,
    "std.array::push_error": 1,
    "std.dict::entry_ref": 2,
    "std.dict::insert_error": 2,
    "std.dict::iter": 2,
    "std.json::decoder": 1,
    "std.json::detached": 1,
    "std.json::reader": 1,
    "std.list::iter": 1,
    "std.list::push_error": 1,
    "std.rc::try_unwrap_result": 1,
    "std.sync::channel": 1,
    "std.sync::lock_result": 1,
    "std.sync::mutex": 1,
    "std.sync::mutex_guard": 1,
    "std.sync::once_lock": 1,
    "std.sync::read_lock_result": 1,
    "std.sync::receiver": 1,
    "std.sync::recv_result": 1,
    "std.sync::rw_lock": 1,
    "std.sync::rw_read_guard": 1,
    "std.sync::rw_write_guard": 1,
    "std.sync::send_result": 1,
    "std.sync::sender": 1,
    "std.sync::set_result": 1,
    "std.sync::sync_channel": 1,
    "std.sync::sync_sender": 1,
    "std.sync::try_lock_result": 1,
    "std.sync::try_read_lock_result": 1,
    "std.sync::try_recv_result": 1,
    "std.sync::try_send_result": 1,
    "std.sync::try_write_lock_result": 1,
    "std.sync::write_lock_result": 1,
    "std.thread::join_result": 1,
}


def is_operation(item: dict[str, Any]) -> bool:
    return item.get("facet") == "operation" and isinstance(item.get("id"), str)


def is_schema_name(name: str) -> bool:
    return "SUFFIX" in name or re.search(r"_[A-Z](?:$|_)", name) is not None


def expanded_operation_names(items: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    operations: dict[str, dict[str, Any]] = {}
    for item in items:
        if not is_operation(item):
            continue
        name = item["id"]
        if not is_schema_name(name):
            operations[name] = {
                "inventory_record": item.get("record_id"),
                "inventory_name": name,
                "source_signature": item.get("source_signature"),
                "normative_rules": item.get("normative_rules", []),
                "expanded_from": None,
            }

    def add_family(schema: str, names: list[str]) -> None:
        item = next((candidate for candidate in items if candidate.get("id") == schema), None)
        if item is None:
            raise ValueError(f"required closed-family inventory record is missing: {schema}")
        for name in names:
            operations.setdefault(
                name,
                {
                    "inventory_record": item.get("record_id"),
                    "inventory_name": schema,
                    "source_signature": item.get("source_signature"),
                    "normative_rules": item.get("normative_rules", []),
                    "expanded_from": schema,
                },
            )

    for stem in (
        "checked_add",
        "checked_sub",
        "checked_mul",
        "wrapping_add",
        "wrapping_sub",
        "wrapping_mul",
        "saturating_add",
        "saturating_sub",
        "saturating_mul",
    ):
        add_family(
            f"core::{stem}_SUFFIX",
            [f"core::{stem}_{suffix}" for suffix in R_INTEGER_SUFFIXES],
        )
    # Library R-LIB-0027 (L45): the bit operations of every integer type and the wide ones of
    # the unsigned types.
    for stem in (
        "leading_zeros",
        "trailing_zeros",
        "count_ones",
        "swap_bytes",
        "rotate_left",
        "rotate_right",
    ):
        add_family(
            f"core::{stem}_SUFFIX",
            [f"core::{stem}_{suffix}" for suffix in R_INTEGER_SUFFIXES],
        )
    for stem in ("widening_mul", "carrying_add", "borrowing_sub", "narrowing_div"):
        add_family(
            f"core::{stem}_SUFFIX",
            [
                f"core::{stem}_{suffix}"
                for suffix in R_INTEGER_SUFFIXES
                if suffix.startswith("u")
            ],
        )
    add_family(
        "std.convert::parse_SUFFIX",
        [f"std.convert::parse_{suffix}" for suffix in NUMERIC_SUFFIXES],
    )
    add_family(
        "std.convert::checked_D",
        [f"std.convert::checked_{suffix}" for suffix in NUMERIC_SUFFIXES],
    )
    add_family(
        "std.c::checked_D",
        [f"std.c::checked_{suffix}" for suffix in C_NUMERIC_SUFFIXES],
    )
    add_family(
        "std.format::append_SUFFIX",
        [f"std.format::append_{suffix}" for suffix in FORMAT_INTEGER_SUFFIXES],
    )
    return dict(sorted(operations.items()))


def expanded_constants(items: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    constants: dict[str, dict[str, Any]] = {}
    for item in items:
        if item.get("facet") != "constant" or not isinstance(item.get("id"), str):
            continue
        name = item["id"]
        if name == "core::min_SUFFIX":
            expanded = [f"core::min_{suffix}" for suffix in R_INTEGER_SUFFIXES]
        elif name == "core::max_SUFFIX":
            expanded = [f"core::max_{suffix}" for suffix in R_INTEGER_SUFFIXES]
        elif is_schema_name(name):
            raise ValueError(f"unsupported public constant schema: {name}")
        else:
            expanded = [name]
        for concrete in expanded:
            constants[concrete] = {
                "inventory_record": item.get("record_id"),
                "inventory_name": name,
                "source_signature": item.get("source_signature"),
                "normative_rules": item.get("normative_rules", []),
                "expanded_from": name if concrete != name else None,
                "value_type": concrete.rsplit("_", 1)[-1],
            }
    return dict(sorted(constants.items()))


def type_generic_arity(name: str, item: dict[str, Any], items: list[dict[str, Any]]) -> int:
    implementation = item.get("implementation")
    if isinstance(implementation, dict) and isinstance(implementation.get("generic_arity"), int):
        return implementation["generic_arity"]
    source_signature = item.get("source_signature")
    if isinstance(source_signature, str):
        match = re.fullmatch(re.escape(name) + r"<([^<>()]*)>", source_signature)
        if match is not None:
            return match.group(1).count(",") + 1
    pattern = re.compile(re.escape(name) + r"<([^<>()]*)>")
    arities: set[int] = set()
    for candidate in items:
        signature = candidate.get("source_signature")
        if not isinstance(signature, str):
            continue
        for match in pattern.finditer(signature):
            arguments = match.group(1)
            if re.search(r"\b[A-Z]\b", arguments):
                arities.add(arguments.count(",") + 1)
    return min(arities) if arities else 0


def concrete_types(items: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    types: dict[str, dict[str, Any]] = {}
    inventory_types = {
        item["id"]: item
        for item in items
        if item.get("facet") == "type" and isinstance(item.get("id"), str)
    }
    for item in items:
        if item.get("facet") != "type" or not isinstance(item.get("id"), str):
            continue
        name = item["id"]
        if name in {"std.math::binary_parts_S", "std.math::fraction_parts_S"}:
            for suffix in ("f32", "f64", "c_float", "c_double", "c_long_double"):
                source_type = name[:-1] + suffix
                types[source_type] = {
                    "inventory_record": item.get("record_id"),
                    "inventory_name": name,
                    "source_signature": item.get("source_signature"),
                    "normative_rules": item.get("normative_rules", []),
                    "generic_arity": 0,
                    "expanded_from": name,
                    "inventory_missing": False,
                }
            continue
        if is_schema_name(name):
            continue
        arity = STANDARD_PARAMETRIC_TYPES.get(
            name, type_generic_arity(name, item, items)
        )
        arguments = R_SOURCE_TYPE_ARGUMENTS.get(name, ["i32"] * arity)
        if name in {"std.json::reader", "std.json::detached"}:
            arguments = ["std.io::input"]
        source_type = name if arity == 0 else f"{name}<{','.join(arguments)}>"
        types[source_type] = {
            "inventory_record": item.get("record_id"),
            "inventory_name": name,
            "item_kind": item.get("item_kind"),
            "source_signature": item.get("source_signature"),
            "normative_rules": item.get("normative_rules", []),
            "generic_arity": arity,
            "expanded_from": None,
            "inventory_missing": False,
        }
    for name, arity in STANDARD_PARAMETRIC_TYPES.items():
        if name in inventory_types:
            continue
        related = next(
            (
                item
                for item in items
                if item.get("facet") == "operation" and item.get("id") == name
            ),
            {},
        )
        source_type = f"{name}<{','.join(['i32'] * arity)}>"
        types[source_type] = {
            "inventory_record": None,
            "inventory_name": name,
            "source_signature": None,
            "normative_rules": related.get("normative_rules", ["R-TYPE-0012"]),
            "generic_arity": arity,
            "expanded_from": None,
            "inventory_missing": True,
        }
    return dict(sorted(types.items()))


def parse_diagnostics(stderr: str, source_name: str) -> list[dict[str, Any]]:
    try:
        parsed = json.loads(stderr)
    except json.JSONDecodeError:
        return [{"message": stderr.strip()}] if stderr.strip() else []
    if not isinstance(parsed, list):
        return [{"message": stderr.strip()}]
    diagnostics = []
    for value in parsed:
        if not isinstance(value, dict):
            diagnostics.append({"message": str(value)})
            continue
        diagnostic = dict(value)
        if "source" in diagnostic:
            diagnostic["source"] = source_name
        diagnostics.append(diagnostic)
    return diagnostics


def probe_operation(
    root: Path,
    compiler: Path,
    directory: Path,
    index: int,
    name: str,
    metadata: dict[str, Any],
    timeout: float,
) -> dict[str, Any]:
    source_name = f"operation_{index:04d}.r"
    path = directory / source_name
    source = (
        f"module source_surface.operation_{index:04d};\n"
        f"{r_source_import(name)}"
        f"void probe() {{ unsafe {{ {name}(); }} }}\n"
    )
    path.write_text(source, encoding="utf-8")
    record = {"name": name, **metadata, "source": source}
    try:
        process = subprocess.run(
            [
                str(compiler),
                "--profile=hosted-native-async",
                "--emit=hir",
                "--diagnostics=json",
                *EXTRA_COMPILER_ARGUMENTS,
                str(path),
            ],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
            cwd=root,
        )
    except subprocess.TimeoutExpired:
        record.update(status="timeout", returncode=None, diagnostics=[])
        return record
    diagnostics = parse_diagnostics(process.stderr, source_name)
    unresolved = any(
        diagnostic.get("message") == UNRESOLVED_MESSAGE for diagnostic in diagnostics
    )
    unimplemented = any(
        diagnostic.get("message") == UNIMPLEMENTED_MESSAGE for diagnostic in diagnostics
    )
    if unresolved:
        status = "unresolved"
    elif unimplemented:
        status = "unimplemented"
    elif process.returncode in (0, 1):
        status = "recognized"
    else:
        status = "compiler_failure"
    record.update(status=status, returncode=process.returncode, diagnostics=diagnostics)
    return record


def probe_constant(
    root: Path,
    compiler: Path,
    directory: Path,
    index: int,
    name: str,
    metadata: dict[str, Any],
    timeout: float,
) -> dict[str, Any]:
    source_name = f"constant_{index:04d}.r"
    path = directory / source_name
    imported = r_source_import(name)
    # A constant of a standard module written in R is read through an import; its type is
    # the declared one, so the probe binds it with auto.
    body = (
        f"void probe() {{ auto value = {name}; value as void; }}\n"
        if imported
        else f"{metadata['value_type']} probe() {{ return {name}; }}\n"
    )
    source = f"module source_surface.constant_{index:04d};\n{imported}{body}"
    path.write_text(source, encoding="utf-8")
    record = {"name": name, **metadata, "source": source}
    try:
        process = subprocess.run(
            [
                str(compiler),
                "--profile=hosted-native-async",
                "--emit=hir",
                "--diagnostics=json",
                *EXTRA_COMPILER_ARGUMENTS,
                str(path),
            ],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
            cwd=root,
        )
    except subprocess.TimeoutExpired:
        record.update(status="timeout", returncode=None, diagnostics=[])
        return record
    diagnostics = parse_diagnostics(process.stderr, source_name)
    unresolved = any(
        diagnostic.get("code") == "R-DIAG-NAME-001" for diagnostic in diagnostics
    )
    if unresolved:
        status = "unresolved"
    elif process.returncode in (0, 1):
        status = "recognized"
    else:
        status = "compiler_failure"
    record.update(status=status, returncode=process.returncode, diagnostics=diagnostics)
    return record


def probe_type(
    root: Path,
    compiler: Path,
    directory: Path,
    index: int,
    source_type: str,
    metadata: dict[str, Any],
    timeout: float,
) -> dict[str, Any]:
    source_name = f"type_{index:04d}.r"
    path = directory / source_name
    inventory_name = metadata["inventory_name"]
    if inventory_name == "std.format::format":
        body = (
            "void probe() throws std.alloc::alloc_error { "
            "std.format::format value = f\"ok\"; drop value; }\n"
        )
    elif inventory_name in R_SOURCE_TYPE_PROBES:
        body = R_SOURCE_TYPE_PROBES[inventory_name] + "\n"
    elif metadata.get("item_kind") == "public_trait":
        body = f"@generic<T: {inventory_name}> void probe(const T* value) {{ value as void; }}\n"
    else:
        body = f"void probe() {{ unsafe {{ raw {source_type}*? value = null; value as void; }} }}\n"
    source = (
        f"module source_surface.type_{index:04d};\n"
        f"{r_source_import(inventory_name)}"
        f"{body}"
        "i32 main() { return 0; }\n"
    )
    path.write_text(source, encoding="utf-8")
    record = {"name": source_type, **metadata, "source": source}
    try:
        process = subprocess.run(
            [
                str(compiler),
                "--profile=hosted-native-async",
                "--emit=llvm-ir",
                "--all-functions",
                "--diagnostics=json",
                *EXTRA_COMPILER_ARGUMENTS,
                str(path),
            ],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
            cwd=root,
        )
    except subprocess.TimeoutExpired:
        record.update(status="timeout", returncode=None, diagnostics=[])
        return record
    diagnostics = parse_diagnostics(process.stderr, source_name)
    if process.returncode == 0 and process.stdout.strip():
        status = "recognized"
    elif any(diagnostic.get("code") == "R-DIAG-NAME-001" for diagnostic in diagnostics):
        status = "unresolved"
    elif process.returncode in (0, 1) and diagnostics:
        status = "rejected"
    else:
        status = "compiler_failure"
    record.update(status=status, returncode=process.returncode, diagnostics=diagnostics)
    return record


def probe_ambiguous_operation(
    root: Path,
    compiler: Path,
    directory: Path,
    index: int,
    name: str,
    body: str,
    timeout: float,
) -> dict[str, Any]:
    source_name = f"operation_signature_{index:04d}.r"
    path = directory / source_name
    source = (
        f"module source_surface.operation_signature_{index:04d};\n"
        f"{body}\n"
        "i32 main() { return 0; }\n"
    )
    path.write_text(source, encoding="utf-8")
    record: dict[str, Any] = {"name": name, "source": source}
    try:
        process = subprocess.run(
            [
                str(compiler),
                "--profile=hosted-native-async",
                "--emit=llvm-ir",
                "--all-functions",
                "--diagnostics=json",
                *EXTRA_COMPILER_ARGUMENTS,
                str(path),
            ],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
            cwd=root,
        )
    except subprocess.TimeoutExpired:
        record.update(status="timeout", returncode=None, diagnostics=[])
        return record
    diagnostics = parse_diagnostics(process.stderr, source_name)
    if process.returncode == 0 and process.stdout.strip():
        status = "accepted"
    elif process.returncode in (0, 1) and diagnostics:
        status = "rejected"
    else:
        status = "compiler_failure"
    record.update(status=status, returncode=process.returncode, diagnostics=diagnostics)
    return record


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=root / "build-debug/r-front")
    parser.add_argument(
        "--inventory",
        type=Path,
        default=root / "library/generated/api_inventory/implementation_inventory.json",
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument(
        "--deny-panic-alloc",
        action="store_true",
        help=(
            "compile every probe under the Core R-OBJ-0012 deny-panic-alloc policy; the "
            "closed library surface must resolve identically because Library R-LIB-0007 "
            "leaves the language new convenience as the only panicking allocation"
        ),
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    arguments = parser.parse_args()
    if arguments.deny_panic_alloc:
        EXTRA_COMPILER_ARGUMENTS.append("--deny-panic-alloc")

    compiler = arguments.compiler.resolve()
    inventory_path = arguments.inventory.resolve()
    if not compiler.is_file():
        parser.error(f"compiler does not exist: {compiler}")
    if not inventory_path.is_file():
        parser.error(f"inventory does not exist: {inventory_path}")
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    if arguments.timeout <= 0:
        parser.error("--timeout must be positive")

    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    items = inventory.get("items")
    if not isinstance(items, list) or not all(isinstance(item, dict) for item in items):
        raise ValueError("inventory items must be an array of objects")
    for module in inventory.get("modules", []):
        if isinstance(module, dict) and module.get("implementation_language") == "r":
            R_SOURCE_MODULES[str(module.get("r_module"))] = str(module.get("source"))
        elif isinstance(module, dict) and isinstance(module.get("r_part"), dict):
            # The R part of a C module loads with the import of the module.
            R_SOURCE_MODULES[str(module.get("r_module"))] = str(module["r_part"].get("source"))
    library_map = root / "library/r/library.map"
    if R_SOURCE_MODULES and library_map.is_file():
        EXTRA_COMPILER_ARGUMENTS.extend(["--library-map", str(library_map)])
    operations = expanded_operation_names(items)
    constants = expanded_constants(items)
    types = concrete_types(items)
    with tempfile.TemporaryDirectory(prefix="r-library-source-surface-") as temporary:
        directory = Path(temporary)
        with ThreadPoolExecutor(max_workers=arguments.jobs) as executor:
            operation_results = list(
                executor.map(
                    lambda pair: probe_operation(
                        root,
                        compiler,
                        directory,
                        pair[0],
                        pair[1][0],
                        pair[1][1],
                        arguments.timeout,
                    ),
                    enumerate(operations.items()),
                )
            )
            constant_results = list(
                executor.map(
                    lambda pair: probe_constant(
                        root,
                        compiler,
                        directory,
                        pair[0],
                        pair[1][0],
                        pair[1][1],
                        arguments.timeout,
                    ),
                    enumerate(constants.items()),
                )
            )
            type_results = list(
                executor.map(
                    lambda pair: probe_type(
                        root,
                        compiler,
                        directory,
                        pair[0],
                        pair[1][0],
                        pair[1][1],
                        arguments.timeout,
                    ),
                    enumerate(types.items()),
                )
            )
            unknown_type_control = probe_type(
                root,
                compiler,
                directory,
                len(types),
                "std.__source_surface_missing::type",
                {
                    "inventory_record": None,
                    "inventory_name": "std.__source_surface_missing::type",
                    "source_signature": None,
                    "normative_rules": ["R-NAME-0003"],
                    "generic_arity": 0,
                },
                arguments.timeout,
            )
            if unknown_type_control["status"] in {"unresolved", "rejected"}:
                unknown_type_control["control_status"] = "passed"
            else:
                unknown_type_control["control_status"] = "failed"
            ambiguous_operation_results = list(
                executor.map(
                    lambda value: probe_ambiguous_operation(
                        root,
                        compiler,
                        directory,
                        value[0],
                        value[1][0],
                        value[1][1],
                        arguments.timeout,
                    ),
                    enumerate(sorted(AMBIGUOUS_OPERATION_PROBES.items())),
                )
            )

    def status_counts(results: list[dict[str, Any]]) -> dict[str, int]:
        return {
            status: sum(result["status"] == status for result in results)
            for status in (
                "recognized",
                "unresolved",
                "unimplemented",
                "rejected",
                "compiler_failure",
                "timeout",
            )
        }

    operation_counts = status_counts(operation_results)
    constant_counts = status_counts(constant_results)
    type_counts = status_counts(type_results)
    inventory_missing_types = [
        result["name"] for result in type_results if result["inventory_missing"]
    ]
    ambiguous_counts = {
        status: sum(result["status"] == status for result in ambiguous_operation_results)
        for status in ("accepted", "rejected", "compiler_failure", "timeout")
    }
    unresolved_by_module: dict[str, list[str]] = {}
    unimplemented_by_module: dict[str, list[str]] = {}
    for result in operation_results + constant_results + type_results:
        if result["status"] not in {"unresolved", "unimplemented"}:
            continue
        module = result["inventory_name"].rsplit("::", 1)[0]
        target = (
            unresolved_by_module
            if result["status"] == "unresolved"
            else unimplemented_by_module
        )
        target.setdefault(module, []).append(result["name"])
    report = {
        "scope": "all concrete public operation and constant names plus concrete public type LLVM representation",
        "compiler": str(compiler),
        "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
        "inventory": str(inventory_path),
        "inventory_sha256": hashlib.sha256(inventory_path.read_bytes()).hexdigest(),
        "operations": {"count": len(operation_results), **operation_counts},
        "ambiguous_operation_signatures": {
            "count": len(ambiguous_operation_results),
            **ambiguous_counts,
        },
        "constants": {"count": len(constant_results), **constant_counts},
        "types": {"count": len(type_results), **type_counts},
        "inventory_missing_types": inventory_missing_types,
        "unknown_type_control": unknown_type_control,
        "unresolved_by_module": unresolved_by_module,
        "unimplemented_by_module": unimplemented_by_module,
        "operation_results": operation_results,
        "ambiguous_operation_results": ambiguous_operation_results,
        "constant_results": constant_results,
        "type_results": type_results,
    }
    output = json.dumps(report, indent=2) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(output, encoding="utf-8")
        print(
            "library source surface: "
            f"{len(operation_results)} operations, {operation_counts['unresolved']} unresolved, "
            f"{operation_counts['unimplemented']} without lowering; "
            f"{ambiguous_counts['rejected']} valid collision signatures rejected; "
            f"{constant_counts['unresolved']} constants unresolved; "
            f"{type_counts['compiler_failure']} type compiler failures; "
            f"{len(inventory_missing_types)} inventory type gaps; "
            f"unknown-type control {unknown_type_control['control_status']}"
        )
    else:
        print(output, end="")
    failed = any(
        counts[status]
        for counts in (operation_counts, constant_counts, type_counts)
        for status in ("unresolved", "unimplemented", "rejected", "compiler_failure", "timeout")
    ) or any(
        ambiguous_counts[status] for status in ("rejected", "compiler_failure", "timeout")
    ) or bool(inventory_missing_types) or unknown_type_control["control_status"] != "passed"
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
