#!/usr/bin/env python3
"""Generate compiler ABI registries for fixed-layout standard types."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


INVENTORY_PATH = Path("library/generated/api_inventory/implementation_inventory.json")
TABLE_PATH = Path("compiler/source/named_standard_copy_abi.generated.inc")
FIXTURE_PATH = Path("tests/fixtures/codegen_core_adopt_named_standard_copy.r")
LAYOUT_FIXTURE_PATH = Path("tests/fixtures/codegen_core_adopt_named_standard_layout.r")
SPECIALIZED_FIXTURE_PATH = Path(
    "tests/fixtures/codegen_core_adopt_compiler_specialized_move.r"
)

# These parameterized Move types use compiler-generated structural storage and glue rather than
# the fixed two-pointer named Move ABI.
COMPILER_SPECIALIZED_MOVE_TYPE_ARITIES = {
    "std.arc::try_unwrap_result": 1,
    "std.async::broadcast_result": 1,
    "std.rc::try_unwrap_result": 1,
    "std.sync::lock_result": 1,
    "std.sync::mutex": 1,
    "std.sync::mutex_guard": 1,
    "std.sync::once_lock": 1,
    "std.sync::read_lock_result": 1,
    "std.sync::recv_result": 1,
    "std.sync::reserve_result": 1,
    "std.sync::rw_lock": 1,
    "std.sync::rw_read_guard": 1,
    "std.sync::rw_write_guard": 1,
    "std.sync::send_result": 1,
    "std.sync::set_result": 1,
    "std.sync::try_lock_result": 1,
    "std.sync::try_read_lock_result": 1,
    "std.sync::try_recv_result": 1,
    "std.sync::try_reserve_result": 1,
    "std.sync::try_send_result": 1,
    "std.sync::try_write_lock_result": 1,
    "std.sync::write_lock_result": 1,
}
COMPILER_SPECIALIZED_MOVE_TYPE_IDS = frozenset(
    COMPILER_SPECIALIZED_MOVE_TYPE_ARITIES
)

FIXED_COPY_TYPE_IDS = (
    "core::memory_order",
    "core::recursion_error",
    "core::utf8_error",
    "std.alloc::alloc_error",
    "std.async::start_error",
    "std.bits::lsb_reader",
    "std.bits::read_error",
    "std.bits::read_error_code",
    "std.bytes::bytes_error",
    "std.hash::md5_digest",
    "std.hash::sha1_digest",
    "std.hash::sha256_digest",
    "std.hash::sha512_digest",
    "std.c::runtime_error",
    "std.c::string_error",
    "std.c::target_info",
    "std.convert::parse_error",
    "std.convert::parse_error_code",
    "std.convert::range_error",
    "std.dict::entry_ref",
    "std.env::env_error",
    "std.env::error_code",
    "std.error::domain",
    "std.error::error",
    "std.format::format_error",
    "std.fs::access",
    "std.fs::create_mode",
    "std.fs::error_code",
    "std.fs::file_kind",
    "std.fs::fs_error",
    "std.fs::metadata",
    "std.fs::open_file_options",
    "std.fs::path_error",
    "std.fs::seek_origin",
    "std.fs::sync_level",
    "std.fs::lock_kind",
    "std.io::error_code",
    "std.io::io_error",
    "std.json::error_code",
    "std.json::value_kind",
    "std.json::options",
    "std.json::mode",
    "std.json::feed_state",
    "std.json::feed_result",
    "std.math::binary_parts_c_double",
    "std.math::binary_parts_c_float",
    "std.math::binary_parts_c_long_double",
    "std.math::binary_parts_f32",
    "std.math::binary_parts_f64",
    "std.math::complex_f32",
    "std.math::complex_f64",
    "std.math::error_code",
    "std.math::fraction_parts_c_double",
    "std.math::fraction_parts_c_float",
    "std.math::fraction_parts_c_long_double",
    "std.math::fraction_parts_f32",
    "std.math::fraction_parts_f64",
    "std.math::math_error",
    "std.net::address_error",
    "std.net::address_error_code",
    "std.net::error_code",
    "std.net::family",
    "std.net::ip_address",
    "std.net::datagram",
    "std.net::listen_options",
    "std.net::net_error",
    "std.net::shutdown_direction",
    "std.net::socket_address",
    "std.net::tcp_options",
    "std.net::udp_options",
    "std.net::peer_credentials",
    "std.net::unix_message",
    "std.process::error_code",
    "std.process::exit_status",
    "std.process::pipe_mode",
    "std.process::process_error",
    "std.process::stdio",
    "std.process::termination_kind",
    "std.signal::kind",
    "std.string::boundary_error",
    "std.string::string_error",
    "std.sync::barrier_error",
    "std.sync::barrier_wait_result",
    "std.thread::thread_error",
    "std.time::duration",
    "std.time::duration_error",
    "std.time::error_code",
    "std.time::instant",
    "std.time::system_time",
    "std.time::time_error",
    "std.time::utc_datetime",
)

C_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
FIXED_COPY_TYPE_ARITIES = {
    "std.dict::entry_ref": 2,
}

# These Copy descriptors contain a hidden runtime borrow. They can be copied within their valid
# scope, but the language intentionally rejects returning them without an explicit borrow origin.
BORROW_BACKED_COPY_TYPE_IDS = {
    "std.dict::entry_ref",
}

# These fixed C layouts are Move-only at the R level, but they are not part of the general named
# Move ABI. core::adopt/release only needs their exact layout and destruction operation: neither
# intrinsic moves the pointee. Types registered in the named Move ABI derive the same information
# from that registry. The three effect-dependent thread handle/result schemas use the compiler's
# existing specialized layout and are therefore intentionally absent.
ADOPT_LAYOUT_TYPES: dict[str, tuple[int, str | None]] = {
    "std.dict::iter": (2, None),
    "std.list::iter": (1, None),
}

# R-BORROW-0018: a guard, a lock outcome carrying one, a scoped thread handle, a container
# iterator and an entry reference hold a region, so no owner may hold them and core::adopt
# rejects them.
REGION_HOLDING_TYPE_IDS = frozenset(
    {
        "std.dict::entry_ref",
        "std.dict::iter",
        "std.list::iter",
        "std.sync::lock_result",
        "std.sync::mutex_guard",
        "std.sync::read_lock_result",
        "std.sync::rw_read_guard",
        "std.sync::rw_write_guard",
        "std.sync::try_lock_result",
        "std.sync::try_read_lock_result",
        "std.sync::try_write_lock_result",
        "std.sync::write_lock_result",
        "std.thread::scoped_join_handle",
    }
)

SPECIALIZED_ADOPT_TYPE_SPELLINGS = {
    "std.thread::join_handle": "std.thread::join_handle<i32>",
    "std.thread::join_result": "std.thread::join_result<i32>",
    "std.thread::scoped_join_handle": "std.thread::scoped_join_handle<i32>",
}
SPECIALIZED_ADOPT_TYPE_IDS = frozenset(SPECIALIZED_ADOPT_TYPE_SPELLINGS)

COMPILER_GENERATED_ADOPT_TYPE_SPELLINGS = {
    "core::atomic_compare_exchange_result": "core::atomic_compare_exchange_result<i32>",
    "std.alloc::new_error": "std.alloc::new_error<i32>",
    "std.array::push_error": "std.array::push_error<i32>",
    "std.dict::insert_error": "std.dict::insert_error<i32, i64>",
    "std.list::push_error": "std.list::push_error<i32>",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_records(root: Path) -> list[dict[str, Any]]:
    inventory_path = root / INVENTORY_PATH
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    require(isinstance(inventory, dict), "inventory root must be an object")
    items = inventory.get("items")
    require(isinstance(items, list), "inventory items must be an array")
    by_id: dict[str, dict[str, Any]] = {}
    for item in items:
        if not isinstance(item, dict) or item.get("facet") != "type":
            continue
        item_id = item.get("id")
        require(isinstance(item_id, str), "type record id must be a string")
        require(item_id not in by_id, f"duplicate type record: {item_id}")
        by_id[item_id] = item

    require(
        len(FIXED_COPY_TYPE_IDS) == 91 and len(set(FIXED_COPY_TYPE_IDS)) == 91,
        "fixed Copy type set must contain exactly 91 unique records",
    )
    require(
        set(FIXED_COPY_TYPE_ARITIES).issubset(FIXED_COPY_TYPE_IDS),
        "generic arity table contains an unknown fixed Copy type",
    )
    records: list[dict[str, Any]] = []
    for item_id in sorted(FIXED_COPY_TYPE_IDS):
        item = by_id.get(item_id)
        require(item is not None, f"missing inventory type record: {item_id}")
        implementation = item.get("implementation")
        require(isinstance(implementation, dict), f"missing implementation: {item_id}")
        require(
            implementation.get("kind") == "header",
            f"fixed Copy type must map to a header: {item_id}",
        )
        header = implementation.get("header")
        c_type = implementation.get("c_type")
        require(isinstance(header, str) and header, f"missing header mapping: {item_id}")
        require(isinstance(c_type, str) and C_IDENTIFIER.fullmatch(c_type) is not None,
                f"invalid C type mapping: {item_id}")
        require((root / header).is_file(), f"mapped header does not exist: {header}")
        records.append(
            {
                "id": item_id,
                "header": Path(header).name,
                "c_type": c_type,
                "generic_arity": FIXED_COPY_TYPE_ARITIES.get(item_id, 0),
                "is_pod": item.get("pod", False),
            }
        )
    return records


def load_layout_records(root: Path) -> list[dict[str, Any]]:
    inventory = json.loads((root / INVENTORY_PATH).read_text(encoding="utf-8"))
    items = inventory.get("items")
    require(isinstance(items, list), "inventory items must be an array")
    by_id = {
        item.get("id"): item
        for item in items
        if isinstance(item, dict) and item.get("facet") == "type"
    }
    records: list[dict[str, Any]] = []
    for item_id, (generic_arity, drop) in sorted(ADOPT_LAYOUT_TYPES.items()):
        item = by_id.get(item_id)
        require(isinstance(item, dict), f"missing adopt layout type record: {item_id}")
        implementation = item.get("implementation")
        require(isinstance(implementation, dict), f"missing implementation: {item_id}")
        require(
            implementation.get("kind") == "header",
            f"adopt layout type must map to a header: {item_id}",
        )
        header = implementation.get("header")
        c_type = implementation.get("c_type")
        require(isinstance(header, str) and header, f"missing header mapping: {item_id}")
        require(
            isinstance(c_type, str) and C_IDENTIFIER.fullmatch(c_type) is not None,
            f"invalid C type mapping: {item_id}",
        )
        require((root / header).is_file(), f"mapped header does not exist: {header}")
        records.append(
            {
                "id": item_id,
                "header": Path(header).name,
                "c_type": c_type,
                "generic_arity": generic_arity,
                "drop": drop,
            }
        )
    require(len(records) == 2, "adopt layout ABI must contain exactly 2 records")
    header_type_ids = {
        item.get("id")
        for item in items
        if isinstance(item, dict)
        and item.get("facet") == "type"
        and isinstance(item.get("id"), str)
        and isinstance(item.get("implementation"), dict)
        and item["implementation"].get("kind") == "header"
    }
    move_type_ids = {
        item.get("id")
        for item in items
        if isinstance(item, dict)
        and item.get("facet") == "type"
        and isinstance(item.get("id"), str)
        and isinstance(item.get("implementation"), dict)
        and isinstance(item["implementation"].get("type_glue"), dict)
    }
    coverage_groups = (
        ("fixed Copy", set(FIXED_COPY_TYPE_IDS)),
        ("named Move", move_type_ids),
        ("adopt layout", set(ADOPT_LAYOUT_TYPES)),
        ("specialized adopt", SPECIALIZED_ADOPT_TYPE_IDS),
        ("compiler-specialized Move", COMPILER_SPECIALIZED_MOVE_TYPE_IDS),
    )
    for left_index, (left_name, left) in enumerate(coverage_groups):
        for right_name, right in coverage_groups[left_index + 1 :]:
            overlap = sorted(left & right)
            require(
                not overlap,
                f"named standard ABI coverage groups must be disjoint: "
                f"{left_name}/{right_name} overlap={overlap}",
            )
    covered_type_ids = set().union(*(group for _, group in coverage_groups))
    require(
        covered_type_ids == header_type_ids,
        "named standard ABI coverage must exactly match inventory header types; "
        f"missing={sorted(header_type_ids - covered_type_ids)}, "
        f"extra={sorted(covered_type_ids - header_type_ids)}",
    )
    return records


def render_table(records: list[dict[str, Any]], layout_records: list[dict[str, Any]]) -> str:
    headers = sorted({record["header"] for record in records})
    layout_headers = sorted({record["header"] for record in layout_records})
    include_headers = sorted(set(headers) | set(layout_headers))
    require(len(headers) <= 64, "fixed Copy header set exceeds the C17 emitter bitset")
    require(len(layout_headers) <= 64, "adopt layout header set exceeds the C17 emitter bitset")
    require(headers[0] == "r_core.h", "r_core.h must be the first generated project header")
    header_indices = {header: index for index, header in enumerate(headers)}
    layout_header_indices = {header: index for index, header in enumerate(layout_headers)}
    lines = [
        "/* Generated by tools/generate_named_standard_copy_abi.py. */",
        *[f'#include "{header}"' for header in include_headers],
        "",
        "static const char *const r_named_standard_copy_abi_headers[] = {",
    ]
    for header in headers:
        lines.append(f'    "{header}",')
    lines.extend(("};", "", "static const RNamedStandardCopyAbi r_named_standard_copy_abi_records[] = {"))
    for record in records:
        name = record["id"]
        lines.extend(
            (
                "    {",
                f'        "{name}",',
                f'        sizeof("{name}") - 1U,',
                f'        "{record["c_type"]}",',
                f'        UINT32_C({record["generic_arity"]}),',
                f'        UINT32_C({header_indices[record["header"]]}),',
                f'        {str(record["is_pod"]).lower()},',
                f'        sizeof({record["c_type"]}),',
                f'        _Alignof({record["c_type"]}),',
                "    },",
            )
        )
    lines.extend(("};", ""))
    lines.append("static const char *const r_named_standard_layout_abi_headers[] = {")
    for header in layout_headers:
        lines.append(f'    "{header}",')
    lines.extend(("};", "", "static const RNamedStandardLayoutAbi r_named_standard_layout_abi_records[] = {"))
    for index, record in enumerate(layout_records):
        name = record["id"]
        drop = "NULL" if record["drop"] is None else f'"{record["drop"]}"'
        lines.extend(
            (
                "    {",
                f'        "{name}",',
                f'        sizeof("{name}") - 1U,',
                f'        "{record["c_type"]}",',
                f"        {drop},",
                f'        UINT32_C({record["generic_arity"]}),',
                f'        UINT32_C({layout_header_indices[record["header"]]}),',
                f"        UINT32_C({index}),",
                f'        sizeof({record["c_type"]}),',
                f'        _Alignof({record["c_type"]}),',
                "    },",
            )
        )
    lines.extend(("};", ""))
    return "\n".join(lines)


def render_fixture(records: list[dict[str, Any]]) -> str:
    lines = [
        "module codegen.core_adopt_named_standard_copy;",
        "",
    ]
    for index, record in enumerate(records, start=1):
        type_name = record["id"]
        if record["generic_arity"] == 2:
            type_name += "<u32, u64>"
        if record["id"] not in BORROW_BACKED_COPY_TYPE_IDS:
            lines.extend(
                (
                f"{type_name} copy_{index:03d}({type_name} value, {type_name}* output) {{",
                "    *output = value;",
                "    return value;",
                "}",
                "",
                )
            )
        if record["id"] in REGION_HOLDING_TYPE_IDS:
            continue
        lines.extend(
            (
                f"void round_trip_{index:03d}(raw {type_name}* pointer) {{",
                "    unsafe {",
                f"        own {type_name}* owner = core::adopt(pointer);",
                f"        raw {type_name}* returned = core::release(move owner);",
                "        returned as void;",
                "    }",
                "}",
                "",
            )
        )
    lines.extend(("i32 main() {", "    return 0;", "}", ""))
    return "\n".join(lines)


def render_layout_fixture(records: list[dict[str, Any]]) -> str:
    lines = ["module codegen.core_adopt_named_standard_layout;", ""]
    for index, record in enumerate(records, start=1):
        if record["id"] in REGION_HOLDING_TYPE_IDS:
            continue
        type_name = record["id"]
        if record["generic_arity"] == 1:
            type_name += "<i32>"
        elif record["generic_arity"] == 2:
            type_name += "<i32, i64>"
        lines.extend(
            (
                f"void round_trip_{index:03d}(raw {type_name}* pointer) {{",
                "    unsafe {",
                f"        own {type_name}* owner = core::adopt(pointer);",
                f"        raw {type_name}* returned = core::release(move owner);",
                "        returned as void;",
                "    }",
                "}",
                "",
            )
        )
    lines.extend(("i32 main() {", "    return 0;", "}", ""))
    return "\n".join(lines)


def render_specialized_fixture() -> str:
    lines = ["module codegen.core_adopt_compiler_specialized_move;", ""]
    type_spellings = dict(SPECIALIZED_ADOPT_TYPE_SPELLINGS)
    type_spellings.update(COMPILER_GENERATED_ADOPT_TYPE_SPELLINGS)
    for type_id, arity in COMPILER_SPECIALIZED_MOVE_TYPE_ARITIES.items():
        if type_id in REGION_HOLDING_TYPE_IDS:
            continue
        type_name = type_id
        if arity == 1:
            type_name += "<i32>"
        elif arity == 2:
            type_name += "<i32, i64>"
        type_spellings[type_id] = type_name
    for index, (type_id, type_name) in enumerate(sorted(type_spellings.items()), start=1):
        if type_id in REGION_HOLDING_TYPE_IDS:
            continue
        lines.extend(
            (
                f"void round_trip_{index:03d}(raw {type_name}* pointer) {{",
                "    unsafe {",
                f"        own {type_name}* owner = core::adopt(pointer);",
                f"        raw {type_name}* returned = core::release(move owner);",
                "        returned as void;",
                "    }",
                "}",
                "",
            )
        )
    lines.extend(("i32 main() {", "    return 0;", "}", ""))
    return "\n".join(lines)


def update_file(path: Path, expected: str, write: bool) -> bool:
    if write:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(expected, encoding="utf-8", newline="\n")
        return True
    try:
        actual = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        print(f"missing generated file: {path}", file=sys.stderr)
        return False
    if actual != expected:
        print(f"stale generated file: {path}", file=sys.stderr)
        return False
    return True


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--verify", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        root = arguments.root.resolve()
        records = load_records(root)
        layout_records = load_layout_records(root)
        outputs = (
            (root / TABLE_PATH, render_table(records, layout_records)),
            (root / FIXTURE_PATH, render_fixture(records)),
            (root / LAYOUT_FIXTURE_PATH, render_layout_fixture(layout_records)),
            (root / SPECIALIZED_FIXTURE_PATH, render_specialized_fixture()),
        )
        return 0 if all(update_file(path, text, arguments.write) for path, text in outputs) else 1
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"named standard Copy ABI generation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
