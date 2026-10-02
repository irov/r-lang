#!/usr/bin/env python3
"""Validate R Standard Library source ownership and inventory coverage."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

from generate_library_inventory import (
    ITEM_FACETS,
    MODULES,
    build_inventory,
    canonical_json,
    facet_for_item_kind,
    load_library_map,
    make_record_id,
    r_source_declaration,
)


IMPLEMENTATION_KINDS = {
    "abi_alias",
    "generated",
    "header",
    "intrinsic",
    "r_source",
    "source",
    "unimplemented",
}
CONFORMANCE_STATUSES = {"complete", "partial"}
FORBIDDEN_SOURCE_NAMES = {"all.c", "module.c"}
PUBLIC_FUNCTION_PATTERN = re.compile(
    r"(?ms)^(?!\s*static\b)[A-Za-z_][A-Za-z0-9_\s*]*\b"
    r"(r_(?:core|std)_[a-z0-9_]+)\s*\([^;{}]*\)\s*\{"
)
INTERNAL_PUBLIC_PREFIX_PATTERN = re.compile(r"\br_(?:core|std)_[a-z0-9_]+\s*\(")


class LayoutErrors:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.messages.append(message)

    def fail(self, message: str) -> None:
        self.messages.append(message)


def load_inventory(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("inventory root must be an object")
    return value


def expected_filename(item_id: str) -> str:
    operation = item_id.split("::", maxsplit=1)[1]
    return operation.lower() + ".c"


def is_beneath(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def validate_modules(
    root: Path,
    inventory: dict[str, Any],
    errors: LayoutErrors,
) -> dict[str, dict[str, Any]]:
    modules = inventory.get("modules")
    if not isinstance(modules, list):
        errors.fail("inventory modules must be an array")
        return {}

    expected = {r_name: (target, directory, token) for r_name, target, directory, token in MODULES}
    r_source = {
        r_name: (source, profile)
        for r_name, source, profile in load_library_map(root)
        if r_name not in expected
    }
    r_parts = {
        r_name: (source, profile)
        for r_name, source, profile in load_library_map(root)
        if r_name in expected
    }
    by_name: dict[str, dict[str, Any]] = {}
    targets: set[str] = set()
    for record in modules:
        if not isinstance(record, dict) or not isinstance(record.get("r_module"), str):
            errors.fail("every module record must be an object with r_module")
            continue
        r_module = record["r_module"]
        if r_module in by_name:
            errors.fail(f"duplicate module record: {r_module}")
            continue
        by_name[r_module] = record
        if record.get("implementation_language") == "r":
            # Library R-SLIB-RSRC-0001: the module is one R source of the library map.
            entry = r_source.get(r_module)
            if entry is None:
                errors.fail(f"R-source module {r_module} is absent from {load_library_map.__doc__ and 'library/r/library.map'}")
                continue
            source, profile = entry
            errors.require(record.get("source") == source, f"wrong source for {r_module}")
            errors.require(record.get("minimum_profile") == profile, f"wrong profile for {r_module}")
            source_path = root / source
            errors.require(source_path.is_file(), f"missing R source for {r_module}: {source}")
            if source_path.is_file():
                declared = re.search(r"(?m)^module\s+([A-Za-z0-9_.]+)\s*;", source_path.read_text(encoding="utf-8"))
                errors.require(
                    declared is not None and declared.group(1) == r_module,
                    f"{source} does not declare module {r_module}",
                )
            continue
        part = r_parts.get(r_module)
        if part is None:
            errors.require("r_part" not in record, f"{r_module} has an R part outside the library map")
        else:
            # Library R-SLIB-RSRC-0001: the R part of a C module is one R source of the map.
            source, profile = part
            errors.require(
                record.get("r_part") == {"source": source, "minimum_profile": profile},
                f"wrong R part for {r_module}",
            )
            source_path = root / source
            errors.require(source_path.is_file(), f"missing R part for {r_module}: {source}")
            if source_path.is_file():
                declared = re.search(r"(?m)^module\s+([A-Za-z0-9_.]+)\s*;", source_path.read_text(encoding="utf-8"))
                errors.require(
                    declared is not None and declared.group(1) == r_module,
                    f"{source} does not declare module {r_module}",
                )
        target = record.get("cmake_target")
        if not isinstance(target, str):
            errors.fail(f"module {r_module} has no cmake_target")
        elif target in targets:
            errors.fail(f"duplicate CMake target in inventory: {target}")
        else:
            targets.add(target)

    errors.require(
        set(by_name) == set(expected) | set(r_source),
        "inventory module set differs from the closed module set",
    )
    for r_module, (target, relative_directory, token) in expected.items():
        record = by_name.get(r_module)
        if record is None or record.get("implementation_language") == "r":
            continue
        errors.require(record.get("cmake_target") == target, f"wrong target for {r_module}")
        errors.require(record.get("directory") == relative_directory, f"wrong directory for {r_module}")
        expected_symbol = f"r_library_internal_module_{token}_descriptor"
        errors.require(
            record.get("descriptor_symbol") == expected_symbol,
            f"wrong descriptor symbol for {r_module}",
        )

        module_directory = root / relative_directory
        cmake_path = module_directory / "CMakeLists.txt"
        errors.require(module_directory.is_dir(), f"missing module directory: {relative_directory}")
        if not cmake_path.is_file():
            errors.fail(f"missing module CMakeLists.txt: {relative_directory}")
            continue
        cmake_text = cmake_path.read_text(encoding="utf-8")
        invocation = re.compile(
            rf"r_add_library_module\(\s*{re.escape(target)}\s+"
            rf"{re.escape(r_module)}\s+{re.escape(token)}\s*",
            re.MULTILINE,
        )
        errors.require(
            invocation.search(cmake_text) is not None,
            f"{relative_directory}/CMakeLists.txt does not declare {target}",
        )
    return by_name


def validate_inventory_freshness(
    specification: Path,
    inventory: dict[str, Any],
    errors: LayoutErrors,
) -> None:
    expected = build_inventory(specification, inventory)
    errors.require(
        canonical_json(inventory) == canonical_json(expected),
        "implementation inventory is stale or omits a normative public item",
    )


def validate_items(
    root: Path,
    inventory: dict[str, Any],
    modules: dict[str, dict[str, Any]],
    errors: LayoutErrors,
) -> None:
    items = inventory.get("items")
    if not isinstance(items, list):
        errors.fail("inventory items must be an array")
        return

    record_ids: set[str] = set()
    mapped_sources: dict[Path, str] = {}
    for item in items:
        if not isinstance(item, dict):
            errors.fail("every item record must be an object")
            continue
        record_id = item.get("record_id")
        item_id = item.get("id")
        facet = item.get("facet")
        module = item.get("module")
        item_kind = item.get("item_kind")
        implementation = item.get("implementation")
        if not isinstance(record_id, str):
            errors.fail("item has no string record_id")
            continue
        if not isinstance(item_id, str):
            errors.fail(f"item {record_id} has no string id")
            continue
        if not isinstance(facet, str) or facet not in ITEM_FACETS:
            errors.fail(f"item {record_id} has unknown facet {facet}")
            continue
        expected_record_id = make_record_id(item_id, facet)
        if record_id != expected_record_id:
            errors.fail(
                f"item {record_id} identity must be {expected_record_id} for its id and facet"
            )
            continue
        if record_id in record_ids:
            errors.fail(f"duplicate public item record: {record_id}")
            continue
        record_ids.add(record_id)
        if not isinstance(item_kind, str):
            errors.fail(f"item {record_id} has no string item_kind")
            continue
        try:
            expected_facet = facet_for_item_kind(item_id, item_kind)
        except ValueError as error:
            errors.fail(str(error))
            continue
        if facet != expected_facet:
            errors.fail(
                f"item {record_id} facet {facet} conflicts with item_kind {item_kind}"
            )
            continue
        if module not in modules:
            errors.fail(f"item {record_id} belongs to unknown module {module}")
            continue
        if not isinstance(implementation, dict):
            errors.fail(f"item {record_id} has no implementation record")
            continue
        kind = implementation.get("kind")
        if kind not in IMPLEMENTATION_KINDS:
            errors.fail(f"item {record_id} has unknown implementation kind {kind}")
            continue
        conformance_status = implementation.get("conformance_status", "complete")
        if conformance_status not in CONFORMANCE_STATUSES:
            errors.fail(
                f"item {record_id} has unknown conformance status {conformance_status}"
            )
            continue
        if conformance_status == "partial":
            errors.require(
                kind != "unimplemented",
                f"partial item {record_id} cannot use unimplemented implementation kind",
            )
            generator_contract = implementation.get("generator_contract")
            errors.require(
                isinstance(generator_contract, str) and bool(generator_contract.strip()),
                f"partial item {record_id} requires a nonempty generator_contract",
            )
        if kind == "header":
            header_value = implementation.get("header")
            c_type = implementation.get("c_type")
            if not isinstance(header_value, str) or not isinstance(c_type, str):
                errors.fail(f"header item {record_id} requires header and c_type strings")
                continue
            header_path = (root / header_value).resolve()
            module_directory = (root / modules[module]["directory"]).resolve()
            errors.require(
                is_beneath(header_path, module_directory / "include"),
                f"header for {record_id} is outside its module include directory",
            )
            errors.require(
                header_path.is_file(), f"missing header for {record_id}: {header_value}"
            )
            if header_path.is_file():
                header_text = header_path.read_text(encoding="utf-8")
                errors.require(
                    re.search(rf"\b{re.escape(c_type)}\b", header_text) is not None,
                    f"{header_value} does not declare C type {c_type}",
                )
            continue
        if kind == "r_source":
            source_value = implementation.get("source")
            r_symbol = implementation.get("r_symbol")
            if not isinstance(source_value, str) or not isinstance(r_symbol, str):
                errors.fail(f"r_source item {record_id} requires source and r_symbol strings")
                continue
            errors.require(r_symbol == item_id, f"r_symbol of {record_id} must be {item_id}")
            errors.require(
                (
                    modules[module].get("implementation_language") == "r"
                    and modules[module].get("source") == source_value
                )
                or modules[module].get("r_part", {}).get("source") == source_value,
                f"source for {record_id} is not the R source of its module",
            )
            source_path = root / source_value
            errors.require(source_path.is_file(), f"missing R source for {record_id}: {source_value}")
            if source_path.is_file():
                declaration = r_source_declaration(
                    source_path.read_text(encoding="utf-8"), item_id.split("::", maxsplit=1)[1]
                )
                errors.require(
                    declaration is not None,
                    f"{source_value} does not declare the public item {item_id}",
                )
            continue
        if kind != "source":
            forbidden_fields = {"source", "c_symbol"} & set(implementation)
            errors.require(
                not forbidden_fields,
                f"non-source item {record_id} carries source fields {sorted(forbidden_fields)}",
            )
            continue

        source_value = implementation.get("source")
        c_symbol = implementation.get("c_symbol")
        if not isinstance(source_value, str) or not isinstance(c_symbol, str):
            errors.fail(f"source item {record_id} requires source and c_symbol strings")
            continue
        source_path = (root / source_value).resolve()
        module_directory = (root / modules[module]["directory"]).resolve()
        errors.require(
            is_beneath(source_path, module_directory / "source"),
            f"source for {record_id} is outside its module source directory",
        )
        errors.require(
            source_path.is_file(), f"missing source for {record_id}: {source_value}"
        )
        if implementation.get("filename_exception") is not True:
            errors.require(
                source_path.name == expected_filename(item_id),
                f"source filename for {record_id} must be {expected_filename(item_id)}",
            )
        if source_path in mapped_sources:
            errors.fail(
                f"source {source_value} implements both {mapped_sources[source_path]} and "
                f"{record_id}"
            )
        else:
            mapped_sources[source_path] = record_id
        if not source_path.is_file():
            continue
        source_text = source_path.read_text(encoding="utf-8")
        symbols = PUBLIC_FUNCTION_PATTERN.findall(source_text)
        errors.require(
            symbols == [c_symbol],
            f"{source_value} must define exactly public symbol {c_symbol}; found {symbols}",
        )
        cmake_path = module_directory / "CMakeLists.txt"
        if cmake_path.is_file():
            cmake_text = cmake_path.read_text(encoding="utf-8")
            errors.require(
                f"source/{source_path.name}" in cmake_text,
                f"{source_value} is not listed in its module CMake target",
            )

    for module in modules.values():
        if module.get("implementation_language") == "r":
            continue
        source_directory = root / module["directory"] / "source"
        if not source_directory.exists():
            continue
        for source_path in source_directory.rglob("*.c"):
            relative = source_path.relative_to(root)
            if source_path.name in FORBIDDEN_SOURCE_NAMES:
                errors.fail(f"forbidden amalgamation/module source: {relative}")
            if source_path.resolve() not in mapped_sources:
                errors.fail(f"module source has no implementation record: {relative}")


def validate_internal_boundary(root: Path, errors: LayoutErrors) -> None:
    internal = root / "library/internal"
    descriptor = internal / "diagnostics/source/module_descriptor.c"
    errors.require(descriptor.is_file(), "missing shared module descriptor source")
    for source_path in internal.rglob("*.c"):
        source_text = source_path.read_text(encoding="utf-8")
        if INTERNAL_PUBLIC_PREFIX_PATTERN.search(source_text):
            errors.fail(
                f"internal source defines or declares a public operation prefix: "
                f"{source_path.relative_to(root)}"
            )


def validate_completeness(inventory: dict[str, Any], errors: LayoutErrors) -> None:
    items = inventory.get("items")
    if not isinstance(items, list):
        return
    unimplemented = sorted(
        item["record_id"]
        for item in items
        if isinstance(item, dict)
        and isinstance(item.get("record_id"), str)
        and isinstance(item.get("implementation"), dict)
        and item["implementation"].get("kind") == "unimplemented"
    )
    preview_limit = 20
    if unimplemented:
        preview = ", ".join(unimplemented[:preview_limit])
        if len(unimplemented) > preview_limit:
            preview += f", ... and {len(unimplemented) - preview_limit} more"
        errors.fail(
            f"{len(unimplemented)} public item records remain unimplemented: {preview}"
        )
    partial = sorted(
        item["record_id"]
        for item in items
        if isinstance(item, dict)
        and isinstance(item.get("record_id"), str)
        and isinstance(item.get("implementation"), dict)
        and item["implementation"].get("conformance_status") == "partial"
    )
    if partial:
        preview = ", ".join(partial[:preview_limit])
        if len(partial) > preview_limit:
            preview += f", ... and {len(partial) - preview_limit} more"
        errors.fail(f"{len(partial)} public item records remain partial: {preview}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--specification", type=Path)
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--require-complete", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    specification = arguments.specification or (
        root / "specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc"
    )
    inventory_path = arguments.inventory or (
        root / "library/generated/api_inventory/implementation_inventory.json"
    )
    try:
        inventory = load_inventory(inventory_path)
        errors = LayoutErrors()
        modules = validate_modules(root, inventory, errors)
        validate_inventory_freshness(specification, inventory, errors)
        validate_items(root, inventory, modules, errors)
        validate_internal_boundary(root, errors)
        if arguments.require_complete:
            validate_completeness(inventory, errors)
        if errors.messages:
            for message in errors.messages:
                print(f"library layout error: {message}", file=sys.stderr)
            return 1
        items = inventory.get("items", [])
        unimplemented_count = sum(
            isinstance(item, dict)
            and isinstance(item.get("implementation"), dict)
            and item["implementation"].get("kind") == "unimplemented"
            for item in items
        )
        partial_count = sum(
            isinstance(item, dict)
            and isinstance(item.get("implementation"), dict)
            and item["implementation"].get("conformance_status") == "partial"
            for item in items
        )
        print(
            f"library layout valid: {len(modules)} modules, {len(items)} public item records, "
            f"{unimplemented_count} unimplemented, {partial_count} partial"
        )
        return 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"library layout check failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
