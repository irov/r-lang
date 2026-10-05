#!/usr/bin/env python3
"""Generate the auditable R Standard Library implementation inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MODULES = (
    ("core", "r_core", "library/core", "core"),
    ("std.alloc", "r_std_alloc", "library/std/alloc", "std_alloc"),
    ("std.arc", "r_std_arc", "library/std/arc", "std_arc"),
    ("std.rc", "r_std_rc", "library/std/rc", "std_rc"),
    ("std.array", "r_std_array", "library/std/array", "std_array"),
    ("std.list", "r_std_list", "library/std/list", "std_list"),
    ("std.dict", "r_std_dict", "library/std/dict", "std_dict"),
    ("std.error", "r_std_error", "library/std/error", "std_error"),
    ("std.bytes", "r_std_bytes", "library/std/bytes", "std_bytes"),
    ("std.hash", "r_std_hash", "library/std/hash", "std_hash"),
    ("std.utf8", "r_std_utf8", "library/std/utf8", "std_utf8"),
    ("std.bits", "r_std_bits", "library/std/bits", "std_bits"),
    ("std.secret", "r_std_secret", "library/std/secret", "std_secret"),
    ("std.random", "r_std_random", "library/std/random", "std_random"),
    ("std.test", "r_std_test", "library/std/test", "std_test"),
    ("std.string", "r_std_string", "library/std/string", "std_string"),
    ("std.convert", "r_std_convert", "library/std/convert", "std_convert"),
    ("std.format", "r_std_format", "library/std/format", "std_format"),
    ("std.json", "r_std_json", "library/std/json", "std_json"),
    ("std.math", "r_std_math", "library/std/math", "std_math"),
    ("std.time", "r_std_time", "library/std/time", "std_time"),
    ("std.env", "r_std_env", "library/std/env", "std_env"),
    ("std.thread", "r_std_thread", "library/std/thread", "std_thread"),
    ("std.sync", "r_std_sync", "library/std/sync", "std_sync"),
    ("std.async", "r_std_async", "library/std/async", "std_async"),
    ("std.io", "r_std_io", "library/std/io", "std_io"),
    ("std.fs", "r_std_fs", "library/std/fs", "std_fs"),
    ("std.net", "r_std_net", "library/std/net", "std_net"),
    ("std.process", "r_std_process", "library/std/process", "std_process"),
    ("std.signal", "r_std_signal", "library/std/signal", "std_signal"),
    ("std.c", "r_std_c", "library/std/c", "std_c"),
)

LIBRARY_MAP = "library/r/library.map"
R_SOURCE_PROFILES = ("freestanding", "allocation", "hosted", "hosted-thread", "hosted-native-async")


def repository_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_library_map(root: Path) -> list[tuple[str, str, str]]:
    """Return (module, repository-relative source path, least profile) for every R-source
    standard module of the library map (Library R-SLIB-RSRC-0001)."""

    path = root / LIBRARY_MAP
    if not path.is_file():
        return []
    entries: list[tuple[str, str, str]] = []
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise ValueError(f"{LIBRARY_MAP}:{line_number}: expected MODULE = PATH [PROFILE]")
        module, rest = (part.strip() for part in line.split("=", 1))
        parts = rest.split()
        if not module or not parts or len(parts) > 2:
            raise ValueError(f"{LIBRARY_MAP}:{line_number}: invalid MODULE = PATH [PROFILE] entry")
        profile = parts[1] if len(parts) == 2 else "freestanding"
        if profile not in R_SOURCE_PROFILES:
            raise ValueError(f"{LIBRARY_MAP}:{line_number}: unknown profile {profile}")
        entries.append((module, "library/r/" + parts[0], profile))
    return entries


def r_source_modules(root: Path | None = None) -> dict[str, tuple[str, str]]:
    """The R-source standard modules: library map entries that name no C module."""

    c_modules = {record[0] for record in MODULES}
    return {
        module: (source, profile)
        for module, source, profile in load_library_map(root or repository_root())
        if module not in c_modules
    }


def r_part_modules(root: Path | None = None) -> dict[str, tuple[str, str]]:
    """The R parts of C standard modules (Library R-SLIB-RSRC-0001): library map entries that
    name a C module; `import` of the module also loads its R part."""

    c_modules = {record[0] for record in MODULES}
    return {
        module: (source, profile)
        for module, source, profile in load_library_map(root or repository_root())
        if module in c_modules
    }


R_SOURCE_DECLARATION_KINDS = ("struct", "enum", "error", "trait")


def r_source_declaration(text: str, name: str) -> tuple[str, int] | None:
    """Return (kind, generic arity) of the top-level declaration `name` in one R source, where
    kind is struct, enum, error, trait, constant or function; None when the source does not
    declare it."""

    text = r_source_module_level(text)
    escaped = re.escape(name)
    for kind in R_SOURCE_DECLARATION_KINDS:
        match = re.search(rf"(?m)^{kind}\s+{escaped}\b", text)
        if match is not None:
            return kind, r_source_generic_arity(text, match.start())
    if re.search(rf"(?m)^const\s+[^=\n]*?\b{escaped}\s*=", text) is not None:
        return "constant", 0
    match = re.search(rf"(?m)^(?!\s)(?:[^\n]*?\s)?(?<!::){escaped}\s*\(", text)
    if match is not None and not re.match(r"^(?:struct|enum|error|trait|impl|module|import)\b", match.group(0)):
        return "function", r_source_generic_arity(text, match.start())
    return None


def r_source_module_level(text: str) -> str:
    """The source with the bodies of module-level `@if (...) { ... }` blocks moved to column
    zero: their declarations are module items under a constant condition (Core R-META-0001)."""

    lines = text.splitlines()
    inside = False
    for index, line in enumerate(lines):
        if not inside and re.match(r"@if\s*\(.*\)\s*\{\s*$", line):
            inside = True
            lines[index] = ""
            continue
        if inside and line == "}":
            inside = False
            lines[index] = ""
            continue
        if inside and line.startswith("    "):
            lines[index] = line[4:]
    return "\n".join(lines) + "\n"


def r_source_generic_arity(text: str, declaration_offset: int) -> int:
    """The parameter count of the @generic header that immediately precedes a declaration."""

    preceding = text[:declaration_offset].rstrip()
    # Argument-free attributes such as @discardable may separate the header from the declaration.
    start = preceding.rfind("@generic<")
    if start < 0:
        return 0
    depth = 0
    count = 1
    index = start + len("@generic")
    while index < len(preceding):
        character = preceding[index]
        if character in "(<":
            depth += 1
        elif character == ")" or (character == ">" and preceding[index - 1] != "-"):
            depth -= 1
            if depth == 0:
                break
        elif character == "," and depth == 1:
            count += 1
        index += 1
    if depth != 0 or re.fullmatch(r"(?:\s*@[A-Za-z_]+)*\s*", preceding[index + 1 :]) is None:
        return 0
    return count


def r_source_implementation_for(root: Path, item_id: str) -> dict[str, Any] | None:
    """The inventory record of one item declared by an R-source standard module."""

    module = module_for_item(item_id)
    entry = r_source_modules(root).get(module)
    part = r_part_modules(root).get(module)
    if entry is None and part is None:
        return None
    source, _profile = entry if entry is not None else part
    text = (root / source).read_text(encoding="utf-8")
    declaration = r_source_declaration(text, item_id.split("::", maxsplit=1)[1])
    if declaration is None and part is not None:
        # The item belongs to the C implementation of the module.
        return None
    if declaration is None:
        raise ValueError(f"{source} does not declare the public item {item_id}")
    kind, arity = declaration
    item_kind = (
        "public_trait"
        if kind == "trait"
        else "public_constant"
        if kind == "constant"
        else "public_type_schema"
        if kind != "function" and arity != 0
        else "public_type"
        if kind != "function"
        else "operation"
    )
    implementation: dict[str, Any] = {"kind": "r_source", "source": source, "r_symbol": item_id}
    if kind not in ("function", "constant"):
        implementation["generic_arity"] = arity
    return {"item_kind": item_kind, "implementation": implementation}


QUALIFIED_ITEM_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_.])((?:core|std\.[a-z][a-z0-9]*)::[A-Za-z_][A-Za-z0-9_]*)"
)
QUALIFIED_CALLABLE_HEAD_PATTERN = re.compile(
    r"^(?:(?:async|unsafe)\s+)*"
    r"((?:core|std\.[a-z][a-z0-9]*)::[A-Za-z_][A-Za-z0-9_]*)\s*(?:::\s*<[^()]*>\s*)?\("
)
ANCHOR_PATTERN = re.compile(r"\[\[((?:R-LIB|R-SLIB)-[A-Z0-9-]+)\]\]")
REVISION_PATTERNS = (
    re.compile(r"^:revnumber:\s*(\S+)\s*$", re.MULTILINE),
    re.compile(r"^\|Document revision\|(\S+)\s*$", re.MULTILINE),
)
ITEM_FACETS = frozenset(("constant", "operation", "type"))

ORDINARY_RESULT_TYPES = {
    "core::atomic_compare_exchange": "core::atomic_compare_exchange_result<T>",
    "std.arc::try_unwrap": "std.arc::try_unwrap_result<T>",
    "std.rc::try_unwrap": "std.rc::try_unwrap_result<T>",
}

ORDINARY_OUTCOME_TYPE_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "core::atomic_compare_exchange_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "generated",
            "generator_contract": (
                "The compiler specializes the closed two-variant Copy outcome for the "
                "statically known atomic T. The generated representation contains exactly "
                "exchanged(T observed) and unchanged(T observed); it is an ordinary value "
                "and has no checked-error carrier or runtime symbol."
            ),
        },
    },
    "std.arc::try_unwrap_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/arc/include/r_std_arc.h",
            "c_type": "RStdArcTryUnwrapResult",
        },
    },
    "std.rc::try_unwrap_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/rc/include/r_std_rc.h",
            "c_type": "RStdRcTryUnwrapResult",
        },
    },
}

# Qualified-name extraction deliberately does not guess which public item an unqualified
# operation or type name denotes. When a later rule continues the contract of an item declared
# by an earlier rule using only its unqualified spelling, keep that relationship explicit and
# auditable here.
SUPPLEMENTAL_RULE_ITEM_ATTRIBUTIONS: dict[str, tuple[str, ...]] = {
    "R-SLIB-BITS-0003": ("std.bits::read",),
    "R-SLIB-BITS-0004": ("std.bits::align_byte",),
    "R-SLIB-BYTES-0007": ("std.hash::md5", "std.hash::md5_digest"),
    "R-SLIB-BYTES-0008": ("std.hash::md5",),
    "R-SLIB-SECRET-0002": (
        "std.secret::with_length",
        "std.secret::from_bytes",
        "std.secret::len",
        "std.secret::as_slice",
        "std.secret::as_slice_mut",
    ),
    "R-SLIB-SECRET-0003": ("std.secret::buffer", "std.secret::zeroize"),
    "R-SLIB-SECRET-0004": ("std.secret::constant_time_equal",),
    "R-SLIB-UTF8-0002": ("std.utf8::is_valid", "std.utf8::validate"),
}

# Library R-LIB-0007 leaves the language `new` convenience as the only allocation that panics
# on failure. Any operation that a later revision documents as panicking on allocation failure
# must be listed here: the inventory records it as panics_on_allocation_failure and the compiler
# rejects the call under the Core R-OBJ-0012 deny-panic-alloc policy.
PANIC_ALLOCATION_OPERATIONS: frozenset[str] = frozenset()

# Library R-LIB-0026: closed operations whose result is supplementary information about the
# requested mutation. The inventory records discardable_result and the compiler registry lets a
# direct call in statement position discard the result under Core R-FUNC-0021. R-source
# module methods carry `@discardable` in their declarations instead.
DISCARDABLE_OPERATIONS: frozenset[str] = frozenset(
    (
        "core::replace",
        "core::atomic_exchange",
        "core::atomic_fetch_add",
        "core::atomic_fetch_sub",
        "core::atomic_fetch_and",
        "core::atomic_fetch_or",
        "core::atomic_fetch_xor",
        "std.array::pop",
        "std.array::remove",
        "std.list::pop_front",
        "std.list::pop_back",
        "std.list::remove",
        "std.dict::insert",
        "std.dict::remove",
        "std.fs::seek",
        "std.sync::barrier_wait",
    )
)

# Library R-SLIB-ASYNC-0012: scoped operations borrow a caller view as a task-group loan that
# lasts until the native backend acknowledges completion or cancellation. The inventory records
# `scoped` and the compiler registry admits the call only inside a task_scope (Core R-STMT-0017).
SCOPED_OPERATIONS: frozenset[str] = frozenset(
    (
        "std.io::read_into",
        "std.io::write_from",
        "std.io::write_all_from",
        "std.fs::read_into",
        "std.fs::write_from",
        "std.fs::write_all_from",
        "std.fs::read_at_into",
        "std.fs::write_all_at_from",
        "std.net::tcp_read_into",
        "std.net::tcp_write_from",
        "std.net::tcp_write_all_from",
        "std.net::udp_send_from",
        "std.net::udp_receive_into",
        "std.net::unix_read_into",
        "std.net::unix_write_from",
        "std.net::unix_write_all_from",
        "std.net::unix_send_from",
        "std.net::unix_receive_into",
    )
)

TYPE_ITEM_KINDS = frozenset(
    (
        "public_type",
        "public_type_schema",
        "public_type_specialization",
        # Library R-SLIB-RSRC-0001: a trait of an R-source module shares the type name space.
        "public_trait",
    )
)
OPERATION_ITEM_KINDS = frozenset(
    (
        "intrinsic_closed_family",
        "intrinsic_family",
        "operation",
        "operation_schema",
        "operation_specialization",
        "type_constructor_schema",
    )
)
CONSTANT_ITEM_KINDS = frozenset(("generated_constant_closed_family", "public_constant"))

# These items predate canonical implementation records and therefore retain the detailed
# unclassified item_kind until their implementation slice is specified. Their semantic facet is
# nevertheless closed by the normative declaration and must not be guessed from their spelling.
UNCLASSIFIED_TYPE_ITEMS = frozenset(
    (
        "std.fs::write_file_result",
        "std.net::tcp_connection",
        "std.net::tcp_listener",
        "std.net::tcp_read_result",
        "std.net::tcp_stream",
        "std.net::tcp_write_all_result",
        "std.net::tcp_write_result",
        "std.net::udp_receive_result",
        "std.net::udp_send_result",
        "std.net::udp_socket",
        "std.process::child",
        "std.process::spawn_result",
        "std.process::wait_result",
    )
)
UNCLASSIFIED_OPERATION_ITEMS = frozenset(
    (
        "std.fs::read_file",
        "std.fs::read_file_beneath",
        "std.fs::write_file_atomic_no_replace",
        "std.fs::write_file_atomic_no_replace_beneath",
        "std.net::resolve",
        "std.net::tcp_accept",
        "std.net::tcp_close",
        "std.net::tcp_connect",
        "std.net::tcp_listen",
        "std.net::tcp_listener_close",
        "std.net::tcp_listener_local_address",
        "std.net::tcp_local_address",
        "std.net::tcp_peer_address",
        "std.net::tcp_read",
        "std.net::tcp_shutdown",
        "std.net::tcp_write",
        "std.net::tcp_write_all",
        "std.net::udp_bind",
        "std.net::udp_close",
        "std.net::udp_local_address",
        "std.net::udp_receive_from",
        "std.net::udp_send_to",
        "std.process::exit",
        "std.process::id",
        "std.process::spawn",
        "std.process::take_stderr",
        "std.process::take_stdin",
        "std.process::take_stdout",
        "std.process::terminate",
        "std.process::wait",
    )
)

@dataclass(frozen=True)
class ExplicitPublicItem:
    """One public schema that qualified-name scanning cannot identify reliably."""

    item_id: str
    item_kind: str
    source_signature: str | None
    evidence: str


@dataclass(frozen=True)
class SchemaSpecialization:
    schema_family: str
    source_signature: str | None
    normative_rules: tuple[str, ...]
    source: str | None = None
    c_symbol: str | None = None
    item_kind: str = "operation_specialization"


def split_top_level_types(text: str) -> list[str]:
    """Split a comma-separated R type list without splitting generic arguments."""

    result: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(text):
        if character in "([<":
            depth += 1
        elif character in ")]" or (character == ">" and text[index - 1 : index] != "-"):
            depth -= 1
            if depth < 0:
                raise ValueError(f"unbalanced type list: {text}")
        elif character == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    if depth != 0:
        raise ValueError(f"unbalanced type list: {text}")
    result.append(text[start:].strip())
    if any(not item for item in result):
        raise ValueError(f"empty type in list: {text}")
    return result


def split_top_level_throws(text: str) -> tuple[str, str | None]:
    """Split one function result from its checked effects, ignoring nested task effects."""

    marker = " throws "
    depth = 0
    match_index: int | None = None
    index = 0
    while index < len(text):
        character = text[index]
        if character in "([<":
            depth += 1
        elif character in ")]" or (character == ">" and text[index - 1 : index] != "-"):
            depth -= 1
            if depth < 0:
                raise ValueError(f"unbalanced result type: {text}")
        elif depth == 0 and text.startswith(marker, index):
            if match_index is not None:
                raise ValueError(f"multiple top-level throws clauses: {text}")
            match_index = index
            index += len(marker) - 1
        index += 1
    if depth != 0:
        raise ValueError(f"unbalanced result type: {text}")
    if match_index is None:
        return text.strip(), None
    return (
        text[:match_index].strip(),
        text[match_index + len(marker) :].strip(),
    )


def normalize_checked_signature(item_id: str, signature: str) -> str:
    """Return the draft.5 source spelling and canonicalize its checked effect set."""

    marker = " -> "
    if marker not in signature:
        raise ValueError(f"operation signature has no result arrow: {item_id}: {signature}")
    head, result = signature.rsplit(marker, maxsplit=1)
    if re.search(r"(?<![A-Za-z0-9_])r\(", result) is not None:
        raise ValueError(
            f"legacy r(T,E) source signature is not permitted in draft.5: "
            f"{item_id}: {signature}"
        )
    ordinary_result = ORDINARY_RESULT_TYPES.get(item_id)
    if ordinary_result is not None:
        if result == ordinary_result:
            return signature
        raise ValueError(f"ordinary outcome signature is stale: {item_id}: {signature}")

    value_type, raw_errors = split_top_level_throws(result)
    if raw_errors is None:
        return f"{head}{marker}{result}"
    error_types = split_top_level_types(raw_errors)
    if len(error_types) != len(set(error_types)):
        raise ValueError(f"duplicate checked error type: {item_id}: {signature}")
    normalized_errors = sorted(error_types)
    return f"{head}{marker}{value_type.strip()} throws {', '.join(normalized_errors)}"


def require_current_source_signature(item_id: str, signature: str) -> None:
    """Reject source-level compatibility spellings removed by Library draft.5."""

    marker = " -> "
    if marker not in signature:
        raise ValueError(f"operation signature has no result arrow: {item_id}: {signature}")
    result = signature.rsplit(marker, maxsplit=1)[1]
    if re.search(r"(?<![A-Za-z0-9_])r\(", result) is not None:
        raise ValueError(
            f"legacy r(T,E) source signature is not permitted in draft.5: "
            f"{item_id}: {signature}"
        )


def checked_effect_contract(item_id: str, signature: str) -> dict[str, Any]:
    """Build the deterministic interface-carrier descriptor for one operation."""

    normalized = normalize_checked_signature(item_id, signature)
    head, result = normalized.rsplit(" -> ", maxsplit=1)
    value_type, raw_errors = split_top_level_throws(result)
    if raw_errors is not None:
        errors = split_top_level_types(raw_errors)
    else:
        errors = []
    tag_table = [{"tag": 0, "role": "success", "type": value_type}]
    tag_table.extend(
        {"tag": tag, "role": "checked_error", "type": error_type}
        for tag, error_type in enumerate(errors, start=1)
    )
    is_async = re.match(r"^(?:unsafe\s+)?async\s+", head) is not None
    completion_spelling = value_type
    if errors:
        completion_spelling += f" throws {', '.join(errors)}"
    invocation_value_type = f"task<{completion_spelling}>" if is_async else value_type
    invocation_errors = ["std.async::start_error"] if is_async else errors
    invocation_tag_table = [
        {"tag": 0, "role": "success", "type": invocation_value_type}
    ]
    invocation_tag_table.extend(
        {"tag": tag, "role": "checked_error", "type": error_type}
        for tag, error_type in enumerate(invocation_errors, start=1)
    )
    descriptor = {
        "declaration_kind": "async" if is_async else "synchronous",
        "value_type": value_type,
        "normalized_error_set": errors,
        "tag_table": tag_table,
        "carrier": "explicit_output_parameter" if errors else "none",
        "invocation": {
            "value_type": invocation_value_type,
            "normalized_error_set": invocation_errors,
            "tag_table": invocation_tag_table,
            "carrier": "explicit_output_parameter" if invocation_errors else "none",
        },
        "completion": (
            {
                "value_type": value_type,
                "normalized_error_set": errors,
                "tag_table": tag_table,
                "carrier": "task_completion" if errors else "none",
            }
            if is_async
            else None
        ),
    }
    encoded = json.dumps(
        descriptor, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    descriptor["descriptor_sha256"] = hashlib.sha256(encoded).hexdigest()
    return descriptor


def normalize_item_signature(item_id: str, facet: str, signature: str) -> str:
    if facet == "operation":
        return normalize_checked_signature(item_id, signature)
    return signature


def explicit_operation(
    item_id: str,
    source_signature: str,
    evidence: str | None = None,
    item_kind: str = "operation_schema",
) -> ExplicitPublicItem:
    return ExplicitPublicItem(
        item_id=item_id,
        item_kind=item_kind,
        source_signature=source_signature,
        evidence=evidence or item_id.split("::", maxsplit=1)[1],
    )


INTEGER_SUFFIX_OPERATIONS = (
    *(f"checked_{operation}_SUFFIX" for operation in ("add", "sub", "mul")),
    *(f"wrapping_{operation}_SUFFIX" for operation in ("add", "sub", "mul")),
    *(f"saturating_{operation}_SUFFIX" for operation in ("add", "sub", "mul")),
)
ATOMIC_OPERATIONS = (
    "atomic_load",
    "atomic_store",
    "atomic_exchange",
    "atomic_compare_exchange",
    "atomic_fetch_add",
    "atomic_fetch_sub",
    "atomic_fetch_and",
    "atomic_fetch_or",
    "atomic_fetch_xor",
    "atomic_is_lock_free",
)
CANONICAL_SOURCE_SIGNATURE_IDS = frozenset(
    f"core::{operation}" for operation in ATOMIC_OPERATIONS
)
SHARED_OWNER_OPERATIONS = (
    "clone",
    "clone_weak",
    "downgrade",
    "upgrade",
    "get_mut",
    "try_unwrap",
    "strong_count",
    "weak_count",
    "ptr_eq",
    "into_raw",
    "from_raw",
)
MATH_NON_FAILING_UNARY = (
    "abs",
    "floor",
    "ceil",
    "trunc",
    "round",
    "is_finite",
    "is_infinite",
    "is_nan",
    "is_normal",
    "sign_bit",
)
MATH_NON_FAILING_BINARY = ("copy_sign", "min", "max", "next_after")
MATH_SCALAR_SUFFIXES = ("f32", "f64", "c_float", "c_double", "c_long_double")
MATH_IMPLEMENTED_NON_FAILING_SUFFIXES = MATH_SCALAR_SUFFIXES
MATH_IMPLEMENTED_FALLIBLE_UNARY = (
    "sin",
    "sinh",
    "tan",
    "asin",
    "asinh",
    "atan",
    "atanh",
    "acos",
    "acosh",
    "sqrt",
    "cbrt",
    "tanh",
    "erf",
    "erfc",
    "exp2",
    "exp",
    "expm1",
    "gamma",
    "log_gamma",
    "log",
    "log2",
    "log10",
    "log1p",
    "cos",
    "cosh",
)
MATH_IMPLEMENTED_FALLIBLE_BINARY = ("atan2", "pow", "hypot", "remainder")
MATH_FALLIBLE_UNARY = (
    "sin",
    "cos",
    "tan",
    "asin",
    "acos",
    "atan",
    "sinh",
    "cosh",
    "tanh",
    "asinh",
    "acosh",
    "atanh",
    "exp",
    "exp2",
    "expm1",
    "log",
    "log2",
    "log10",
    "log1p",
    "sqrt",
    "cbrt",
    "erf",
    "erfc",
    "gamma",
    "log_gamma",
)
MATH_FALLIBLE_BINARY = ("atan2", "pow", "hypot", "remainder")
MATH_PARTS_TYPES = ("fraction_parts", "binary_parts")
MATH_PARTS_OPERATIONS = ("split_fraction", "split_binary", "compose_binary")
COMPLEX_NON_FAILING_BINARY = ("add", "sub", "mul")
COMPLEX_NON_FAILING_UNARY = ("conjugate", "phase")
COMPLEX_SUFFIXES = ("complex_f32", "complex_f64")
COMPLEX_FALLIBLE_UNARY = (
    "exp",
    "log",
    "sqrt",
    "sin",
    "cos",
    "tan",
    "sinh",
    "cosh",
    "tanh",
)
COMPLEX_FALLIBLE_BINARY = ("div", "pow")


# Qualified-name extraction is intentionally conservative, but it cannot derive unqualified
# schematic names or items incorporated from the Core Specification by reference. This catalog is
# the deterministic bridge. It also pins qualified schematic families so an incidental prose
# spelling cannot silently become their only inventory record.
EXPLICIT_RULE_ITEMS: dict[str, tuple[ExplicitPublicItem, ...]] = {
    "R-LIB-0001": (
        ExplicitPublicItem(
            "core::min_SUFFIX",
            "generated_constant_closed_family",
            "core::min_SUFFIX: SUFFIX",
            "core::min_SUFFIX",
        ),
        ExplicitPublicItem(
            "core::max_SUFFIX",
            "generated_constant_closed_family",
            "core::max_SUFFIX: SUFFIX",
            "core::max_SUFFIX",
        ),
        *(
            explicit_operation(
                f"core::{operation}",
                f"core::{operation}(SUFFIX left, SUFFIX right) -> "
                + ("o<SUFFIX>" if operation.startswith("checked_") else "SUFFIX"),
                item_kind="intrinsic_closed_family",
            )
            for operation in INTEGER_SUFFIX_OPERATIONS
        ),
        explicit_operation(
            "core::slice_from_raw_parts",
            "unsafe core::slice_from_raw_parts(raw const T*? pointer, usize length) -> const T[]",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::slice_from_raw_parts_mut",
            "unsafe core::slice_from_raw_parts_mut(raw T*? pointer, usize length) -> T[]",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::slice_from_raw_parts_in",
            "unsafe core::slice_from_raw_parts_in(A anchor, raw const T*? pointer, usize length) "
            "-> const T[]",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::slice_from_raw_parts_in_mut",
            "unsafe core::slice_from_raw_parts_in_mut(A anchor, raw T*? pointer, usize length) -> T[]",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::volatile_load",
            "unsafe core::volatile_load(raw const T* address) -> T",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::volatile_store",
            "unsafe core::volatile_store(raw T* address, T value) -> void",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::assume",
            "unsafe core::assume(bool condition) -> void",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::adopt",
            "unsafe core::adopt(raw T* pointer) -> own T*",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::release",
            "unsafe core::release(own T* owner) -> raw T*",
            evidence="R-UNSAFE-0007..0008",
            item_kind="intrinsic_family",
        ),
    ),
    "R-LIB-0025": (
        explicit_operation("core::replace", "core::replace(T* destination, T replacement) -> T", item_kind="intrinsic_family"),
        explicit_operation("core::take", "core::take(T* destination) -> T", item_kind="intrinsic_family"),
        explicit_operation(
            "core::swap", "core::swap(T* first, T* second) -> void", item_kind="intrinsic_family"
        ),
        explicit_operation(
            "core::clone", "core::clone(const T* value) -> T", item_kind="intrinsic_family"
        ),
    ),
    "R-LIB-0024": (
        explicit_operation(
            "core::enum_count",
            "core::enum_count::<T>() -> usize",
            evidence="core::enum_count",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::enum_min",
            "core::enum_min::<T>() -> T",
            evidence="core::enum_min",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::enum_max",
            "core::enum_max::<T>() -> T",
            evidence="core::enum_max",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::enum_variants",
            "core::enum_variants::<T>() -> T[N]",
            evidence="core::enum_variants",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::enum_name",
            "core::enum_name(T value) -> constexpr str",
            evidence="core::enum_name",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::enum_ordinal",
            "core::enum_ordinal(T value) -> usize",
            evidence="core::enum_ordinal",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::enum_at",
            "core::enum_at::<T>(usize index) -> o<T>",
            evidence="core::enum_at",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::enum_from_name",
            "core::enum_from_name::<T>(str name) -> o<T>",
            evidence="core::enum_from_name",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::variant_count",
            "core::variant_count::<T>() -> usize",
            evidence="core::variant_count",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::variant_name",
            "core::variant_name(const T* value) -> constexpr str",
            evidence="core::variant_name",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::type_name",
            "core::type_name::<T>() -> constexpr str",
            evidence="core::type_name",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::field_count",
            "core::field_count::<T>() -> usize",
            evidence="core::field_count",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::field_name",
            "core::field_name::<T>(usize index) -> constexpr str",
            evidence="core::field_name",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::target_name",
            "core::target_name() -> constexpr str",
            evidence="core::target_name",
            item_kind="intrinsic_family",
        ),
        explicit_operation(
            "core::profile_name",
            "core::profile_name() -> constexpr str",
            evidence="core::profile_name",
            item_kind="intrinsic_family",
        ),
    ),
    "R-LIB-0011": tuple(
        explicit_operation(
            f"core::{operation}",
            {
                "atomic_load": (
                    "core::atomic_load(const (atomic T)* obj, core::memory_order order) -> T"
                ),
                "atomic_store": (
                    "core::atomic_store(const (atomic T)* obj, T desired, "
                    "core::memory_order order) -> void"
                ),
                "atomic_exchange": (
                    "core::atomic_exchange(const (atomic T)* obj, T desired, "
                    "core::memory_order order) -> T"
                ),
                "atomic_compare_exchange": (
                    "core::atomic_compare_exchange(const (atomic T)* obj, T expected, "
                    "T desired, core::memory_order success_order, "
                    "core::memory_order failure_order) -> "
                    "core::atomic_compare_exchange_result<T>"
                ),
                "atomic_fetch_add": (
                    "core::atomic_fetch_add(const (atomic T)* obj, T operand, "
                    "core::memory_order order) -> T"
                ),
                "atomic_fetch_sub": (
                    "core::atomic_fetch_sub(const (atomic T)* obj, T operand, "
                    "core::memory_order order) -> T"
                ),
                "atomic_fetch_and": (
                    "core::atomic_fetch_and(const (atomic T)* obj, T operand, "
                    "core::memory_order order) -> T"
                ),
                "atomic_fetch_or": (
                    "core::atomic_fetch_or(const (atomic T)* obj, T operand, "
                    "core::memory_order order) -> T"
                ),
                "atomic_fetch_xor": (
                    "core::atomic_fetch_xor(const (atomic T)* obj, T operand, "
                    "core::memory_order order) -> T"
                ),
                "atomic_is_lock_free": (
                    "core::atomic_is_lock_free(const (atomic T)* obj) -> bool"
                ),
            }[operation],
            evidence=(
                "atomic_fetch_add/sub/and/or/xor"
                if operation.startswith("atomic_fetch_")
                else operation
            ),
            item_kind="intrinsic_family",
        )
        for operation in ATOMIC_OPERATIONS
    ),
    "R-LIB-0012": tuple(
        explicit_operation(
            f"{module}::{operation}",
            {
                "clone": f"{module}::clone(const Owner<T>* source) -> Owner<T>",
                "clone_weak": f"{module}::clone_weak(const Weak<T>* source) -> Weak<T>",
                "downgrade": f"{module}::downgrade(const Owner<T>* source) -> Weak<T>",
                "upgrade": f"{module}::upgrade(const Weak<T>* source) -> o<Owner<T>>",
                "get_mut": f"{module}::get_mut(Owner<T>* owner) -> T*?",
                "try_unwrap": (
                    f"{module}::try_unwrap(Owner<T> owner) -> "
                    f"{module}::try_unwrap_result(T)"
                ),
                "strong_count": f"{module}::strong_count(const Owner<T>* source) -> usize",
                "weak_count": f"{module}::weak_count(const Owner<T>* source) -> usize",
                "ptr_eq": (
                    f"{module}::ptr_eq(const Owner<T>* left, const Owner<T>* right) -> bool"
                ),
                "into_raw": f"{module}::into_raw(Owner<T> owner) -> raw const (T)*",
                "from_raw": (
                    f"unsafe {module}::from_raw(raw const (T)* pointer) -> Owner<T>"
                ),
            }[operation],
            evidence=operation,
        )
        for module in ("std.arc", "std.rc")
        for operation in SHARED_OWNER_OPERATIONS
    ),
    "R-SLIB-CONV-0002": (
        explicit_operation(
            "std.convert::parse_SUFFIX",
            "std.convert::parse_SUFFIX(str source, u32 radix) -> "
            "SUFFIX throws std.convert::parse_error",
            evidence="parse_SUFFIX",
        ),
    ),
    "R-SLIB-CONV-0004": (
        explicit_operation(
            "std.convert::checked_D",
            "std.convert::checked_D(S value) -> D throws std.convert::range_error",
        ),
    ),
    "R-SLIB-FMT-0002": (
        explicit_operation(
            "std.format::append_SUFFIX",
            "std.format::append_SUFFIX(std.format::builder* target, SUFFIX value, "
            "u32 radix) -> void throws std.format::format_error",
        ),
    ),
    "R-SLIB-MATH-0002": (
        *(
            explicit_operation(
                f"std.math::{operation}_S",
                f"std.math::{operation}_S(S value) -> "
                + ("bool" if operation.startswith("is_") or operation == "sign_bit" else "S"),
                evidence=f"{operation}_S",
            )
            for operation in MATH_NON_FAILING_UNARY
        ),
        *(
            explicit_operation(
                f"std.math::{operation}_S",
                {
                    "copy_sign": "std.math::copy_sign_S(S magnitude, S sign) -> S",
                    "min": "std.math::min_S(S left, S right) -> S",
                    "max": "std.math::max_S(S left, S right) -> S",
                    "next_after": "std.math::next_after_S(S from, S toward) -> S",
                }[operation],
                evidence=f"{operation}_S",
            )
            for operation in MATH_NON_FAILING_BINARY
        ),
    ),
    "R-SLIB-MATH-0003": (
        *(
            explicit_operation(
                f"std.math::{operation}_S",
                f"std.math::{operation}_S(S value) -> S throws std.math::math_error",
                evidence=operation,
            )
            for operation in MATH_FALLIBLE_UNARY
        ),
        *(
            explicit_operation(
                f"std.math::{operation}_S",
                f"std.math::{operation}_S(S left, S right) -> "
                "S throws std.math::math_error",
                evidence=operation,
            )
            for operation in MATH_FALLIBLE_BINARY
        ),
    ),
    "R-SLIB-MATH-0004": (
        ExplicitPublicItem(
            "std.math::fraction_parts_S",
            "public_type_schema",
            None,
            "std.math::fraction_parts_S",
        ),
        ExplicitPublicItem(
            "std.math::binary_parts_S",
            "public_type_schema",
            None,
            "std.math::binary_parts_S",
        ),
        explicit_operation(
            "std.math::split_fraction_S",
            "std.math::split_fraction_S(S value) -> std.math::fraction_parts_S",
        ),
        explicit_operation(
            "std.math::split_binary_S",
            "std.math::split_binary_S(S value) -> std.math::binary_parts_S",
        ),
        explicit_operation(
            "std.math::compose_binary_S",
            "std.math::compose_binary_S(S fraction, i32 exponent) -> "
            "S throws std.math::math_error",
        ),
    ),
    "R-SLIB-MATH-0005": (
        *(
            explicit_operation(
                f"std.math::{operation}_C",
                f"std.math::{operation}_C(C left, C right) -> C",
                evidence=f"{operation}_C",
            )
            for operation in COMPLEX_NON_FAILING_BINARY
        ),
        *(
            explicit_operation(
                f"std.math::{operation}_C",
                f"std.math::{operation}_C(C value) -> "
                + ("COMPONENT" if operation == "phase" else "C"),
                evidence=f"{operation}_C",
            )
            for operation in COMPLEX_NON_FAILING_UNARY
        ),
        explicit_operation(
            "std.math::magnitude_C",
            "std.math::magnitude_C(C value) -> COMPONENT throws std.math::math_error",
            evidence="magnitude_C",
        ),
        *(
            explicit_operation(
                f"std.math::{operation}_C",
                f"std.math::{operation}_C(C value) -> C throws std.math::math_error",
                evidence=operation,
            )
            for operation in COMPLEX_FALLIBLE_UNARY
        ),
        *(
            explicit_operation(
                f"std.math::{operation}_C",
                f"std.math::{operation}_C(C left, C right) -> "
                "C throws std.math::math_error",
                evidence=f"{operation}_C",
            )
            for operation in COMPLEX_FALLIBLE_BINARY
        ),
    ),
    "R-SLIB-C-0001": (
        explicit_operation(
            "std.c::checked_D",
            "std.c::checked_D(S value) -> D throws std.convert::range_error",
        ),
    ),
}

# Any edit to a rule whose public surface needs explicit interpretation requires a deliberate
# catalog audit. Without this guard, adding another unqualified OP name could update the document
# hash while leaving the public-item set silently incomplete.
EXPLICIT_RULE_BLOCK_SHA256 = {
    "R-LIB-0025": "536bb3dc8c2d40e442d578aad7199694ad01cb313c0e04996b4f439e582223b4",
    "R-LIB-0001": "5375ba18932b66260e350206caa3bdd284a0d99ccb7365d6715ec86a82a256ac",
    "R-LIB-0011": "bad2e9acdb5d0e246e8b7ae30d39bd311a53a20b06d361b34b6558469c790062",
    "R-LIB-0024": "62d97edd14420baaab3dd506cc5f9573708352273e0e8cc17d8fbe7fa236c7ca",
    "R-LIB-0012": "c2aa9b207f7297de2ab9c470dabf728f2fa1d92c2827b010ef7de32090bc1be1",
    "R-SLIB-C-0001": "49e64346f93cce514fc3d6825645d536f78730d1219412c267f7aadb626baf83",
    "R-SLIB-CONV-0002": "467f4b66ef802d7b779b01c6c1a99a12cfdcef70d10129d56c97f6a672a12a17",
    "R-SLIB-CONV-0004": "fba1382b6e5573aa46908eb2a43acff59774ad84557b2f33073c63819f514a53",
    "R-SLIB-FMT-0002": "c62daff579675445fb89acbec9f0e4c545181cf5797910cf0aaa462a77219c05",
    "R-SLIB-MATH-0002": "b605fcb72a4056b2d72d50d275c5e7e3c43d1a3fbc2301b3f1a17ce6f3669664",
    "R-SLIB-MATH-0003": "77c0da0b6e44b3ddef76b446358a8fa21e12bbbd08270e91f542949b7932b5b3",
    "R-SLIB-MATH-0004": "8fd1906fcc3cc193919c3e2ea86b48d105be3f96f6b4c4a9dcc12c5c73ecca3d",
    "R-SLIB-MATH-0005": "ea5b9ed7e6c374fcd2543ec4aeb220df8a7415f2675360b50bcbfa6db941b4d4",
}

# A qualified example can also be a real literal specialization. Record it once as an exact
# operation and relate it to its family; do not infer that implementing it implements every S.
def math_non_failing_signature(operation: str, suffix: str) -> str:
    if operation in MATH_NON_FAILING_UNARY:
        result = "bool" if operation.startswith("is_") or operation == "sign_bit" else suffix
        parameters = f"{suffix} value"
    elif operation == "copy_sign":
        result = suffix
        parameters = f"{suffix} magnitude, {suffix} sign"
    elif operation == "next_after":
        result = suffix
        parameters = f"{suffix} from, {suffix} toward"
    else:
        result = suffix
        parameters = f"{suffix} left, {suffix} right"
    return f"std.math::{operation}_{suffix}({parameters}) -> {result}"


def math_fallible_unary_signature(operation: str, suffix: str) -> str:
    return (
        f"std.math::{operation}_{suffix}({suffix} value) -> "
        f"{suffix} throws std.math::math_error"
    )


def math_fallible_binary_signature(operation: str, suffix: str) -> str:
    return (
        f"std.math::{operation}_{suffix}({suffix} left, {suffix} right) -> "
        f"{suffix} throws std.math::math_error"
    )


def math_parts_operation_signature(operation: str, suffix: str) -> str:
    if operation == "split_fraction":
        return (
            f"std.math::split_fraction_{suffix}({suffix} value) -> "
            f"std.math::fraction_parts_{suffix}"
        )
    if operation == "split_binary":
        return (
            f"std.math::split_binary_{suffix}({suffix} value) -> "
            f"std.math::binary_parts_{suffix}"
        )
    return (
        f"std.math::compose_binary_{suffix}({suffix} fraction, i32 exponent) -> "
        f"{suffix} throws std.math::math_error"
    )


def math_complex_operation_signature(operation: str, suffix: str) -> str:
    component = suffix.removeprefix("complex_")
    if operation in COMPLEX_NON_FAILING_BINARY:
        return (
            f"std.math::{operation}_{suffix}({suffix} left, {suffix} right) -> {suffix}"
        )
    if operation == "conjugate":
        return f"std.math::conjugate_{suffix}({suffix} value) -> {suffix}"
    if operation == "phase":
        return f"std.math::phase_{suffix}({suffix} value) -> {component}"
    if operation == "magnitude":
        return (
            f"std.math::magnitude_{suffix}({suffix} value) -> "
            f"{component} throws std.math::math_error"
        )
    if operation in COMPLEX_FALLIBLE_UNARY:
        return (
            f"std.math::{operation}_{suffix}({suffix} value) -> "
            f"{suffix} throws std.math::math_error"
        )
    return (
        f"std.math::{operation}_{suffix}({suffix} left, {suffix} right) -> "
        f"{suffix} throws std.math::math_error"
    )


MATH_PARTS_C_SUFFIX_NAMES = {
    "f32": "F32",
    "f64": "F64",
    "c_float": "CFloat",
    "c_double": "CDouble",
    "c_long_double": "CLongDouble",
}


SCHEMA_SPECIALIZATIONS = {
    **{
        f"std.math::{operation}_{suffix}": SchemaSpecialization(
            schema_family=f"std.math::{operation}_S",
            source_signature=math_non_failing_signature(operation, suffix),
            normative_rules=("R-SLIB-MATH-0002", "R-SLIB-MATH-0006"),
            source=f"library/std/math/source/{operation}_{suffix}.c",
            c_symbol=f"r_std_math_{operation}_{suffix}",
        )
        for suffix in MATH_IMPLEMENTED_NON_FAILING_SUFFIXES
        for operation in (*MATH_NON_FAILING_UNARY, *MATH_NON_FAILING_BINARY)
    },
    **{
        f"std.math::{parts_type}_{suffix}": SchemaSpecialization(
            schema_family=f"std.math::{parts_type}_S",
            source_signature=None,
            normative_rules=("R-SLIB-MATH-0004",),
            item_kind="public_type_specialization",
        )
        for suffix in MATH_SCALAR_SUFFIXES
        for parts_type in MATH_PARTS_TYPES
    },
    **{
        f"std.math::{operation}_{suffix}": SchemaSpecialization(
            schema_family=f"std.math::{operation}_S",
            source_signature=math_parts_operation_signature(operation, suffix),
            normative_rules=(
                ("R-SLIB-MATH-0003", "R-SLIB-MATH-0004", "R-SLIB-MATH-0006")
                if operation == "compose_binary"
                else ("R-SLIB-MATH-0004", "R-SLIB-MATH-0006")
            ),
            source=f"library/std/math/source/{operation}_{suffix}.c",
            c_symbol=f"r_std_math_{operation}_{suffix}",
        )
        for suffix in MATH_SCALAR_SUFFIXES
        for operation in MATH_PARTS_OPERATIONS
    },
    **{
        f"std.math::{operation}_{suffix}": SchemaSpecialization(
            schema_family=f"std.math::{operation}_C",
            source_signature=math_complex_operation_signature(operation, suffix),
            normative_rules=(
                ("R-SLIB-MATH-0003", "R-SLIB-MATH-0005", "R-SLIB-MATH-0006")
                if operation == "magnitude"
                else ("R-SLIB-MATH-0005", "R-SLIB-MATH-0006")
            ),
            source=f"library/std/math/source/{operation}_{suffix}.c",
            c_symbol=f"r_std_math_{operation}_{suffix}",
        )
        for suffix in COMPLEX_SUFFIXES
        for operation in (
            *COMPLEX_NON_FAILING_BINARY,
            *COMPLEX_NON_FAILING_UNARY,
            "magnitude",
            *COMPLEX_FALLIBLE_UNARY,
            *COMPLEX_FALLIBLE_BINARY,
        )
    },
    **{
        f"std.math::{operation}_{suffix}": SchemaSpecialization(
            schema_family=f"std.math::{operation}_S",
            source_signature=math_fallible_unary_signature(operation, suffix),
            normative_rules=("R-SLIB-MATH-0002", "R-SLIB-MATH-0003", "R-SLIB-MATH-0006"),
            source=f"library/std/math/source/{operation}_{suffix}.c",
            c_symbol=f"r_std_math_{operation}_{suffix}",
        )
        for operation in MATH_IMPLEMENTED_FALLIBLE_UNARY
        for suffix in MATH_SCALAR_SUFFIXES
    },
    **{
        f"std.math::{operation}_{suffix}": SchemaSpecialization(
            schema_family=f"std.math::{operation}_S",
            source_signature=math_fallible_binary_signature(operation, suffix),
            normative_rules=("R-SLIB-MATH-0002", "R-SLIB-MATH-0003", "R-SLIB-MATH-0006"),
            source=f"library/std/math/source/{operation}_{suffix}.c",
            c_symbol=f"r_std_math_{operation}_{suffix}",
        )
        for operation in MATH_IMPLEMENTED_FALLIBLE_BINARY
        for suffix in MATH_SCALAR_SUFFIXES
    },
}

MATH_NON_FAILING_SCHEMA_GENERATOR_CONTRACT = (
    "The compiler validates the closed target suffix set and selects the matching literal "
    "source specialization. This target provides f32, f64, c_float, c_double and "
    "c_long_double; selection uses the statically known suffix without overload resolution "
    "and emits no schema-family runtime symbol."
)
CORE_INTRINSIC_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "core::enum_count": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_count::<T>() -> usize",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the number of variants of a fieldless enumeration into a usize literal; a generic parameter folds when the instantiation substitutes it."
            ),
        },
    },
    "core::enum_min": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_min::<T>() -> T",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the variant with the least discriminant, in the signedness of the underlying type, into an enum constant."
            ),
        },
    },
    "core::enum_max": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_max::<T>() -> T",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the variant with the greatest discriminant, in the signedness of the underlying type, into an enum constant."
            ),
        },
    },
    "core::enum_variants": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_variants::<T>() -> T[N]",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds every variant in declaration order into a fixed-array initializer of enum constants; the enumeration shall be concrete."
            ),
        },
    },
    "core::enum_name": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_name(T value) -> constexpr str",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "A standard call whose C17 lowering is a switch over the enumeration value assigning the program string of the declared variant name."
            ),
        },
    },
    "core::enum_ordinal": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_ordinal(T value) -> usize",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "A standard call whose C17 lowering is a switch over the enumeration value assigning the zero-based declaration index."
            ),
        },
    },
    "core::enum_at": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_at::<T>(usize index) -> o<T>",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "A standard call whose C17 lowering is a switch over the usize index constructing o::some of the variant at that declaration index, or o::none."
            ),
        },
    },
    "core::enum_from_name": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::enum_from_name::<T>(str name) -> o<T>",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "A standard call whose C17 lowering searches a static const table of the variant names with a translation-unit-local helper and constructs o::some of the matching variant, or o::none."
            ),
        },
    },
    "core::variant_count": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::variant_count::<T>() -> usize",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the number of variants of a tagged union into a usize literal."
            ),
        },
    },
    "core::variant_name": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::variant_name(const T* value) -> constexpr str",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "A standard call over a shared borrow whose C17 lowering is a switch over the active tag assigning the program string of the declared variant name."
            ),
        },
    },
    "core::type_name": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::type_name::<T>() -> constexpr str",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the canonical spelling of Core R-REFL-0003 into a program string; a generic parameter folds when the instantiation substitutes it."
            ),
        },
    },
    "core::field_count": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::field_count::<T>() -> usize",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the number of fields of a complete struct into a usize literal."
            ),
        },
    },
    "core::field_name": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::field_name::<T>(usize index) -> constexpr str",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the declared name of the field at the constant index into a program string and rejects an index beyond the field count."
            ),
        },
    },
    "core::target_name": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::target_name() -> constexpr str",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the target triple of the generated target identity table into a program string."
            ),
        },
    },
    "core::profile_name": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::profile_name() -> constexpr str",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass folds the name of the selected library profile into a program string."
            ),
        },
    },
    "core::atomic_compare_exchange": {
        "item_kind": "intrinsic_family",
        "source_signature": (
            "core::atomic_compare_exchange(const (atomic T)* obj, T expected, T desired, "
            "core::memory_order success_order, core::memory_order failure_order) -> "
            "core::atomic_compare_exchange_result<T>"
        ),
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "Typed C17 atomic compare-exchange lowering validates the success and failure "
                "orders independently, evaluates every operand once, and constructs the "
                "ordinary exchanged(observed) or unchanged(observed) outcome."
            ),
        },
    },
    "core::replace": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::replace(T* destination, T replacement) -> T",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": "R-OWN-0019 proves initialized exclusive access, unborrowed Copy/Move and exact replacement type. Arguments follow ordinary call-free and aliasing rules. HIR/MIR preserve the destination loan and explicit replacement or proven R default. Sync and async C17 stage operands once, move or copy the previous value out, commit the replacement and track both initialization flags. The exchange allocates nothing, adds no panic or checked effect and does not invoke user drop. Exactly-once cleanup follows existing glue.",
            "conformance_status": "complete",
        },
    },
    "core::take": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::take(T* destination) -> T",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": "R-OWN-0019 proves initialized exclusive access, unborrowed Copy/Move and exact replacement type. Arguments follow ordinary call-free and aliasing rules. HIR/MIR preserve the destination loan and explicit replacement or proven R default. Sync and async C17 stage operands once, move or copy the previous value out, commit the replacement and track both initialization flags. The exchange allocates nothing, adds no panic or checked effect and does not invoke user drop. Exactly-once cleanup follows existing glue.",
            "conformance_status": "complete",
        },
    },
    "core::swap": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::swap(T* first, T* second) -> void",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": "R-OWN-0019 proves two initialized exclusive places of one unborrowed Copy/Move type; both loans stay active, so an overlap is the ordinary aliasing diagnostic. Sync and async C17 exchange the values through one temporary with copy or move glue; nothing is dropped, allocated or run in user code.",
            "conformance_status": "complete",
        },
    },
    "core::clone": {
        "item_kind": "intrinsic_family",
        "source_signature": "core::clone(const T* value) -> T",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": "R-OWN-0020 proves the clone capability. A Copy clone lowers to a read through the borrow and keeps view provenance. Any other clone calls generated per-type glue (strings, paths, array, list, dict, options, own, fixed arrays, tuples, shared owners and clone hooks) that builds the copy in uninitialized storage, destroys every built component on failure and reports std.alloc::alloc_error through the checked carrier; the source is unchanged.",
            "conformance_status": "complete",
        },
    },
    "core::adopt": {
        "item_kind": "intrinsic_family",
        "source_signature": "unsafe core::adopt(raw T* pointer) -> own T*",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass requires one non-null mutable raw T pointer in a lexical "
                "unsafe context and infers an outermost-unqualified complete, sized, inhabited "
                "and non-void T. HIR and MIR preserve the inferred pointee and strict C17 "
                "lowering evaluates the pointer once before calling r_runtime_own_adopt with "
                "inline size and alignment metadata. The valid-contract path allocates no "
                "memory, does not copy, move or drop T, and introduces no R panic; the helper "
                "status represents an impossible internal state after the compiler checks. "
                "Runtime conformance tests prove exact pointer identity and no-drop adoption. "
                "Inventory-validated generated ABI registries supply exact headers, C type "
                "spellings, generic arities and destruction callbacks for every inventory-listed "
                "named standard representation. Copy and general Move registries preserve their "
                "existing semantic classifications; a separate adopt-only layout registry covers "
                "the remaining resource and result representations without classifying them as "
                "Copy or enabling value movement. Existing specialized lowering covers the thread "
                "join schemas. Generated strict-C17 tests exercise adopt/release for every registry "
                "entry. "
                "For C17-representable T, the compiler emits recursive move/drop metadata for "
                "named structs, fixed arrays, options, results, dynamic containers, arc/rc/weak "
                "owners and atomic values. Executable generated-C tests cover reverse-order "
                "exactly-once destruction, both result alternatives, relaxed atomic snapshot "
                "initialization, early exits and allocation-failure injection."
            ),
            "generator_contract": (
                "The compiler emits inline size and alignment metadata, reusable recursive "
                "move/drop callbacks and the exact runtime owner ABI for user and derived types. "
                "For named standard representations it selects an exact generated Copy, Move or "
                "adopt-only layout record and emits the required header and drop adapter."
            ),
            "conformance_status": "complete",
        },
    },
    "core::release": {
        "item_kind": "intrinsic_family",
        "source_signature": "unsafe core::release(own T* owner) -> raw T*",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass requires one non-null own T pointer in a lexical unsafe "
                "context, infers an outermost-unqualified complete, sized, inhabited and "
                "non-void T, and requires explicit move for a named owner. HIR and MIR preserve "
                "the consuming move and inferred pointee; sync and async strict C17 lowering "
                "evaluate the owner once, call r_runtime_own_into_raw, return the exact T "
                "pointer and clear the consumed owner's initialized/drop state. The operation "
                "allocates no memory, does not copy, move or drop T, and introduces no R panic; "
                "the helper status represents an impossible internal state after compiler "
                "checks. Runtime conformance tests prove exact pointer identity, exactly-once "
                "state clearing and no drop during release. Inventory-validated generated ABI "
                "registries supply exact headers, C type spellings, generic arities and destruction "
                "callbacks for every inventory-listed named standard representation. The separate "
                "adopt-only layout registry does not change Copy/Move classification or permit value "
                "movement; existing specialized lowering covers the thread join schemas. Generated "
                "strict-C17 tests exercise adopt/release for every registry entry. Recursive "
                "generated metadata preserves "
                "the eventual exactly-once drop duty for C17-representable named structs, fixed "
                "arrays, options, results, dynamic containers, arc/rc/weak owners and atomic "
                "values, including async-frame cleanup."
            ),
            "generator_contract": (
                "The compiler emits consuming owner-state transfer plus the exact runtime raw "
                "pointer ABI for user and derived types. For named standard representations it "
                "selects an exact generated Copy, Move or adopt-only layout record and emits the "
                "required header and drop adapter."
            ),
            "conformance_status": "complete",
        },
    },
    "core::assume": {
        "item_kind": "intrinsic_family",
        "source_signature": "unsafe core::assume(bool condition) -> void",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass requires one bool operand in a lexical unsafe context; HIR "
                "and MIR preserve the intrinsic, and the strict C17 emitter evaluates the "
                "operand once and reaches C undefined behavior only when the asserted contract "
                "is false. No runtime symbol, allocation, panic or synchronization edge is "
                "introduced."
            ),
        },
    },
    "core::slice_from_raw_parts": {
        "item_kind": "intrinsic_family",
        "source_signature": (
            "unsafe core::slice_from_raw_parts(raw const T*? pointer, usize length) -> const T[]"
        ),
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass infers an outermost-unqualified complete, sized and inhabited "
                "T from one compatible raw const pointer, accepts nullable and non-null pointer "
                "values through the standard non-null-to-nullable conversion, and requires an "
                "exact usize length in a lexical unsafe context. The result carries a fresh "
                "nonescaping region represented by its mandatory destination binding; return "
                "escape and liveness across await are rejected. HIR and MIR preserve the "
                "intrinsic and inferred pointee. Strict C17 lowering evaluates pointer then "
                "length once and initializes exactly one shared slice descriptor without a "
                "runtime symbol, allocation, element access, copy or panic. The caller retains "
                "the complete R-SAFETY-SLICE-PARTS duty, including null only at zero length."
                " Borrow-bearing aggregate fields and aggregate returns remain outside the "
                "current frontend slice and are diagnosed with R-DIAG-SLICE-001; they are never "
                "treated as static or origin-free values."
            ),
        },
    },
    "core::slice_from_raw_parts_mut": {
        "item_kind": "intrinsic_family",
        "source_signature": (
            "unsafe core::slice_from_raw_parts_mut(raw T*? pointer, usize length) -> T[]"
        ),
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass infers an outermost-unqualified complete, sized and inhabited "
                "T from one compatible mutable raw pointer, accepts nullable and non-null pointer "
                "values through the standard non-null-to-nullable conversion, and requires an "
                "exact usize length in a lexical unsafe context. The result carries a fresh "
                "exclusive nonescaping region represented by its mandatory destination binding; "
                "return escape and liveness across await are rejected. HIR and MIR preserve the "
                "intrinsic and inferred pointee. Strict C17 lowering evaluates pointer then "
                "length once and initializes exactly one mutable slice descriptor without a "
                "runtime symbol, allocation, element access, copy or panic. The caller retains "
                "the complete R-SAFETY-SLICE-PARTS duty, including null only at zero length."
                " Borrow-bearing aggregate fields and aggregate returns remain outside the "
                "current frontend slice and are diagnosed with R-DIAG-SLICE-001; they are never "
                "treated as static or origin-free values."
            ),
        },
    },
    "core::slice_from_raw_parts_in": {
        "item_kind": "intrinsic_family",
        "source_signature": (
            "unsafe core::slice_from_raw_parts_in(A anchor, raw const T*? pointer, usize length) "
            "-> const T[]"
        ),
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass applies the core::slice_from_raw_parts pointer, element and "
                "length rules in a lexical unsafe context and lowers the anchor first: a borrow, "
                "slice or str value, a written &place borrowing the place shared. The anchor "
                "shall be a place, a field or dereference of one, or a borrow of one; it is "
                "never evaluated and the result takes its borrow origin instead of a fresh "
                "region, so the result may be stored and returned wherever a borrow derived "
                "from the anchor could, while the ordinary borrow rules keep the anchor live and "
                "unmoved. HIR and MIR keep the core::slice_from_raw_parts operation with the "
                "pointer and length operands and mark the node anchored; strict C17 lowering is "
                "the unanchored descriptor without a runtime symbol, allocation, element access, "
                "copy or panic. The caller retains the R-SAFETY-SLICE-PARTS duty for the "
                "anchored region."
            ),
        },
    },
    "core::slice_from_raw_parts_in_mut": {
        "item_kind": "intrinsic_family",
        "source_signature": (
            "unsafe core::slice_from_raw_parts_in_mut(A anchor, raw T*? pointer, usize length) -> T[]"
        ),
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass applies the core::slice_from_raw_parts_mut pointer, element "
                "and length rules in a lexical unsafe context and lowers the anchor first: an "
                "exclusive borrow or mutable slice value, a written &place borrowing the place "
                "exclusively. The anchor shall be a place, a field or dereference of one, or a "
                "borrow of one; it is never evaluated and the result takes its borrow origin, so "
                "the ordinary borrow rules keep the anchor live, unmoved and otherwise unused "
                "while the result is live. HIR and MIR keep the core::slice_from_raw_parts_mut "
                "operation with the pointer and length operands and mark the node anchored; "
                "strict C17 lowering is the unanchored descriptor without a runtime symbol, "
                "allocation, element access, copy or panic. The caller retains the "
                "R-SAFETY-SLICE-PARTS duty for the anchored region."
            ),
        },
    },
    "core::volatile_load": {
        "item_kind": "intrinsic_family",
        "source_signature": "unsafe core::volatile_load(raw const T* address) -> T",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass infers T from one non-null raw const pointer in a lexical "
                "unsafe context and accepts only the closed complete, sized, inhabited Copy "
                "value family of R-UNSAFE-0008. HIR and MIR retain the inferred pointee type; "
                "strict C17 lowering evaluates the address once and performs exactly one "
                "const volatile T read. No runtime symbol, allocation, panic, atomic operation "
                "or synchronization edge is introduced."
            ),
        },
    },
    "core::volatile_store": {
        "item_kind": "intrinsic_family",
        "source_signature": "unsafe core::volatile_store(raw T* address, T value) -> void",
        "implementation": {
            "kind": "intrinsic",
            "compiler_contract": (
                "The semantic pass infers T from one non-null mutable raw pointer in a lexical "
                "unsafe context, requires one exact T value, and accepts only the closed "
                "complete, sized, inhabited Copy value family of R-UNSAFE-0008. HIR and MIR "
                "retain the inferred pointee type; strict C17 lowering evaluates address then "
                "value once and performs exactly one volatile T write. No runtime symbol, "
                "allocation, panic, atomic operation or synchronization edge is introduced."
            ),
        },
    },
}


MATH_NON_FAILING_SCHEMA_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    f"std.math::{operation}_S": {
        "item_kind": "operation_schema",
        "source_signature": math_non_failing_signature(operation, "S"),
        "implementation": {
            "kind": "generated",
            "generator_contract": MATH_NON_FAILING_SCHEMA_GENERATOR_CONTRACT,
        },
    }
    for operation in (*MATH_NON_FAILING_UNARY, *MATH_NON_FAILING_BINARY)
}

MATH_FALLIBLE_SCHEMA_GENERATOR_CONTRACT = (
    "The compiler selects the matching source-backed operation from the complete five-member "
    "f32, f64, c_float, c_double and c_long_double specialization set using the statically "
    "known suffix. The closed family performs no overload resolution and emits no "
    "schema-family runtime symbol."
)
MATH_FALLIBLE_SCHEMA_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    f"std.math::{operation}_S": {
        "item_kind": "operation_schema",
        "source_signature": (
            math_fallible_unary_signature(operation, "S")
            if operation in MATH_IMPLEMENTED_FALLIBLE_UNARY
            else math_fallible_binary_signature(operation, "S")
        ),
        "implementation": {
            "kind": "generated",
            "generator_contract": MATH_FALLIBLE_SCHEMA_GENERATOR_CONTRACT,
        },
    }
    for operation in (*MATH_IMPLEMENTED_FALLIBLE_UNARY, *MATH_IMPLEMENTED_FALLIBLE_BINARY)
}

MATH_PARTS_TYPE_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    f"std.math::{parts_type}_{suffix}": {
        "item_kind": "public_type_specialization",
        "implementation": {
            "kind": "header",
            "header": "library/std/math/include/r_std_math.h",
            "c_type": (
                f"RStdMath{''.join(word.title() for word in parts_type.split('_'))}"
                f"{MATH_PARTS_C_SUFFIX_NAMES[suffix]}"
            ),
        },
    }
    for suffix in MATH_SCALAR_SUFFIXES
    for parts_type in MATH_PARTS_TYPES
}

MATH_PARTS_TYPE_SCHEMA_GENERATOR_CONTRACT = (
    "The compiler selects the matching Copy header layout from the five literal f32, f64, "
    "c_float, c_double and c_long_double type specializations using the statically known "
    "suffix; no schema-family runtime object exists."
)
MATH_PARTS_OPERATION_SCHEMA_GENERATOR_CONTRACT = (
    "The compiler selects the matching source-backed operation from the five literal f32, "
    "f64, c_float, c_double and c_long_double specializations using the statically known "
    "suffix without overload resolution or a schema-family runtime symbol."
)
MATH_PARTS_SCHEMA_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    **{
        f"std.math::{parts_type}_S": {
            "item_kind": "public_type_schema",
            "implementation": {
                "kind": "generated",
                "generator_contract": MATH_PARTS_TYPE_SCHEMA_GENERATOR_CONTRACT,
            },
        }
        for parts_type in MATH_PARTS_TYPES
    },
    **{
        f"std.math::{operation}_S": {
            "item_kind": "operation_schema",
            "source_signature": math_parts_operation_signature(operation, "S"),
            "implementation": {
                "kind": "generated",
                "generator_contract": MATH_PARTS_OPERATION_SCHEMA_GENERATOR_CONTRACT,
            },
        }
        for operation in MATH_PARTS_OPERATIONS
    },
}

MATH_COMPLEX_TYPE_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.math::complex_f32": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/math/include/r_std_math.h",
            "c_type": "RStdMathComplexF32",
        },
    },
    "std.math::complex_f64": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/math/include/r_std_math.h",
            "c_type": "RStdMathComplexF64",
        },
    },
}

MATH_COMPLEX_SCHEMA_GENERATOR_CONTRACT = (
    "The compiler selects the matching complex_f32 or complex_f64 source specialization "
    "from the statically known complex type. The closed family performs no overload "
    "resolution and emits no schema-family runtime symbol."
)
MATH_COMPLEX_SCHEMA_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    f"std.math::{operation}_C": {
        "item_kind": "operation_schema",
        "source_signature": EXPLICIT_RULE_ITEMS["R-SLIB-MATH-0005"][index].source_signature,
        "implementation": {
            "kind": "generated",
            "generator_contract": MATH_COMPLEX_SCHEMA_GENERATOR_CONTRACT,
        },
    }
    for index, operation in enumerate(
        (
            *COMPLEX_NON_FAILING_BINARY,
            *COMPLEX_NON_FAILING_UNARY,
            "magnitude",
            *COMPLEX_FALLIBLE_UNARY,
            *COMPLEX_FALLIBLE_BINARY,
        )
    )
}

CHECKED_SCHEMA_KERNEL_CONTRACT = (
    "The listed C symbol is the type-erased checked-conversion kernel. The reference compiler "
    "recognizes the closed suffix set and emits exact call-site carrier/result lowering for "
    "statically typed values. The complete source/destination matrix and deterministic link-plan "
    "integration are verified. Target-manifest-driven ABI generation supplies C spellings, "
    "carrier storage, runtime range descriptors and host representation assertions; C17 emission "
    "validates the exact selected target-manifest identity before producing output."
)

# R-LIB-0019 (P4.2): the std.array operation added after the original catalogue.
STD_ARRAY_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.array::filled": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.array::filled(usize length, T value) -> array<T> throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/array/source/filled.c",
            "c_symbol": "r_std_array_filled",
        },
    },
}

STD_CONVERT_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.convert::checked_D": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.convert::checked_D(S value) -> D throws std.convert::range_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/convert/source/checked.c",
            "c_symbol": "r_std_convert_checked",
            "generator_contract": CHECKED_SCHEMA_KERNEL_CONTRACT,
            "filename_exception": True,
        },
    },
    **{
        f"std.convert::parse_{suffix}": {
            "item_kind": "operation",
            "source_signature": (
                f"std.convert::parse_{suffix}(str source) -> "
                f"{suffix} throws std.convert::parse_error"
            ),
            "implementation": {
                "kind": "source",
                "source": f"library/std/convert/source/parse_{suffix}.c",
                "c_symbol": f"r_std_convert_parse_{suffix}",
            },
        }
        for suffix in ("f32", "f64", "c_float", "c_double", "c_long_double")
    },
}

STD_BYTES_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.bytes::with_capacity": {
        "item_kind": "operation",
        "source_signature": (
            "std.bytes::with_capacity(usize capacity) -> "
            "bytes throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/bytes/source/with_capacity.c",
            "c_symbol": "r_std_bytes_with_capacity",
        },
    },
    **{
        f"std.bytes::{operation}": {
            "item_kind": "operation",
            "source_signature": signature,
            "implementation": {
                "kind": "source",
                "source": f"library/std/bytes/source/{operation}.c",
                "c_symbol": f"r_std_bytes_{operation}",
            },
        }
        for operation, signature in (
            (
                "append",
                "std.bytes::append(bytes* target, const u8[] source) -> "
                "void throws std.alloc::alloc_error",
            ),
            (
                "append_u8",
                "std.bytes::append_u8(bytes* target, u8 value) -> "
                "void throws std.alloc::alloc_error",
            ),
            (
                "append_u16_le",
                "std.bytes::append_u16_le(bytes* target, u16 value) -> "
                "void throws std.alloc::alloc_error",
            ),
            (
                "append_u32_le",
                "std.bytes::append_u32_le(bytes* target, u32 value) -> "
                "void throws std.alloc::alloc_error",
            ),
            (
                "append_u64_le",
                "std.bytes::append_u64_le(bytes* target, u64 value) -> "
                "void throws std.alloc::alloc_error",
            ),
        )
    },
}

STD_HASH_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    **{
        f"std.hash::{operation}": {
            "item_kind": "operation",
            "source_signature": signature,
            "implementation": {
                "kind": "source",
                "source": f"library/std/hash/source/{operation}.c",
                "c_symbol": f"r_std_hash_{operation}",
            },
        }
        for operation, signature in (
            ("crc32", "std.hash::crc32(const u8[] source) -> u32"),
            (
                "md5",
                "std.hash::md5(const u8[] source) -> std.hash::md5_digest",
            ),
            (
                "sha1",
                "std.hash::sha1(const u8[] source) -> std.hash::sha1_digest",
            ),
            (
                "sha256",
                "std.hash::sha256(const u8[] source) -> std.hash::sha256_digest",
            ),
            (
                "sha512",
                "std.hash::sha512(const u8[] source) -> std.hash::sha512_digest",
            ),
        )
    },
    **{
        f"std.hash::{digest}": {
            "item_kind": "public_type",
            "implementation": {
                "kind": "header",
                "header": "library/std/hash/include/r_std_hash.h",
                "c_type": c_type,
            },
        }
        for digest, c_type in (
            ("md5_digest", "RStdHashMd5Digest"),
            ("sha1_digest", "RStdHashSha1Digest"),
            ("sha256_digest", "RStdHashSha256Digest"),
            ("sha512_digest", "RStdHashSha512Digest"),
        )
    },
}

STD_UTF8_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    f"std.utf8::{operation}": {
        "item_kind": "operation",
        "source_signature": signature,
        "implementation": {
            "kind": "source",
            "source": f"library/std/utf8/source/{operation}.c",
            "c_symbol": f"r_std_utf8_{operation}",
        },
    }
    for operation, signature in (
        ("is_valid", "std.utf8::is_valid(const u8[] source) -> bool"),
        (
            "validate",
            "std.utf8::validate(const u8[] source) -> str throws core::utf8_error",
        ),
    )
}

STD_BITS_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.bits::read": {
        "item_kind": "operation",
        "source_signature": (
            "std.bits::read(const u8[] source, std.bits::lsb_reader* reader, "
            "u8 width) -> u64 throws std.bits::read_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/bits/source/read.c",
            "c_symbol": "r_std_bits_read",
        },
    },
    "std.bits::align_byte": {
        "item_kind": "operation",
        "source_signature": (
            "std.bits::align_byte(std.bits::lsb_reader* reader) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/bits/source/align_byte.c",
            "c_symbol": "r_std_bits_align_byte",
        },
    },
    **{
        f"std.bits::{type_name}": {
            "item_kind": "public_type",
            "implementation": {
                "kind": "header",
                "header": "library/std/bits/include/r_std_bits.h",
                "c_type": c_type,
            },
        }
        for type_name, c_type in (
            ("read_error_code", "RStdBitsReadErrorCode"),
            ("read_error", "RStdBitsReadError"),
            ("lsb_reader", "RStdBitsLsbReader"),
        )
    },
}

STD_SECRET_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.secret::buffer": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/secret/include/r_std_secret.h",
            "c_type": "RStdSecretBuffer",
            "type_glue": {
                "move_initialize": "r_std_secret_buffer_move_initialize",
                "drop": "r_std_secret_buffer_destroy",
                "linkage": "static_inline",
            },
        },
    },
    **{
        f"std.secret::{operation}": {
            "item_kind": "operation",
            "source_signature": signature,
            "implementation": {
                "kind": "source",
                "source": f"library/std/secret/source/{operation}.c",
                "c_symbol": f"r_std_secret_{operation}",
            },
        }
        for operation, signature in (
            (
                "with_length",
                "std.secret::with_length(usize length) -> "
                "std.secret::buffer throws std.alloc::alloc_error",
            ),
            ("from_bytes", "std.secret::from_bytes(bytes source) -> std.secret::buffer"),
            ("len", "std.secret::len(const std.secret::buffer* source) -> usize"),
            (
                "as_slice",
                "std.secret::as_slice(const std.secret::buffer* source) -> const u8[]",
            ),
            ("as_slice_mut", "std.secret::as_slice_mut(std.secret::buffer* source) -> u8[]"),
            ("zeroize", "std.secret::zeroize(u8[] target) -> void"),
            (
                "constant_time_equal",
                "std.secret::constant_time_equal(const u8[] left, const u8[] right) -> bool",
            ),
        )
    },
}

# R-SLIB-RANDOM-0001 (M20): the C part of std.random; its R part is library/r/std/random.r.
STD_RANDOM_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.random::fill": {
        "item_kind": "operation",
        "source_signature": "std.random::fill(u8[] target) -> void",
        "implementation": {
            "kind": "source",
            "source": "library/std/random/source/fill.c",
            "c_symbol": "r_std_random_fill",
        },
    },
}

# R-SLIB-TEST-0003 (M24): the C part of std.test; its R part is library/r/std/test.r.
STD_TEST_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.test::allocation_attempts": {
        "item_kind": "operation",
        "source_signature": "std.test::allocation_attempts() -> u64",
        "implementation": {
            "kind": "source",
            "source": "library/std/test/source/allocation_attempts.c",
            "c_symbol": "r_std_test_allocation_attempts",
        },
    },
    "std.test::fail_allocation_at": {
        "item_kind": "operation",
        "source_signature": "std.test::fail_allocation_at(u64 attempt) -> void",
        "implementation": {
            "kind": "source",
            "source": "library/std/test/source/fail_allocation_at.c",
            "c_symbol": "r_std_test_fail_allocation_at",
        },
    },
}

# R-SLIB-SIGNAL-0001..0003 (M22): process signals delivered as task completions.
STD_SIGNAL_HEADER = "library/std/signal/include/r_std_signal.h"
STD_SIGNAL_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.signal::kind": {
        "item_kind": "public_type",
        "implementation": {"kind": "header", "header": STD_SIGNAL_HEADER, "c_type": "RStdSignalKind"},
    },
    "std.signal::listener": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": STD_SIGNAL_HEADER,
            "c_type": "RStdSignalListener",
            "type_glue": {
                "move_initialize": "r_std_signal_listener_move_initialize",
                "drop": "r_std_signal_listener_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.signal::listen": {
        "item_kind": "operation",
        "source_signature": (
            "std.signal::listen(std.signal::kind kind) -> std.signal::listener "
            "throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/signal/source/listen.c",
            "c_symbol": "r_std_signal_listen",
        },
    },
    "std.signal::next": {
        "item_kind": "operation",
        "source_signature": (
            "async std.signal::next(const std.signal::listener* listener, "
            "o<std.time::instant> deadline) -> u64 throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/signal/source/next.c",
            "c_symbol": "r_std_signal_next",
        },
    },
    "std.signal::raise": {
        "item_kind": "operation",
        "source_signature": (
            "std.signal::raise(std.signal::kind kind) -> void throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/signal/source/raise.c",
            "c_symbol": "r_std_signal_raise",
        },
    },
}

STD_C_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.c::c_string": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/c/include/r_std_c.h",
            "c_type": "RStdCString",
            "type_glue": {
                "move_initialize": "r_std_c_string_move_initialize",
                "drop": "r_std_c_string_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.c::handle": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/c/include/r_std_c.h",
            "c_type": "RStdCHandle",
            "type_glue": {
                "move_initialize": "r_std_c_handle_move_initialize",
                "drop": "r_std_c_handle_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.c::thread_attachment": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/c/include/r_std_c.h",
            "c_type": "RStdCThreadAttachment",
            "type_glue": {
                "move_initialize": "r_std_c_thread_attachment_move_initialize",
                "drop": "r_std_c_thread_attachment_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.c::checked_D": {
        "item_kind": "operation_schema",
        "source_signature": "std.c::checked_D(S value) -> D throws std.convert::range_error",
        "implementation": {
            "kind": "source",
            "source": "library/std/c/source/checked.c",
            "c_symbol": "r_std_c_checked",
            "generator_contract": CHECKED_SCHEMA_KERNEL_CONTRACT,
            "filename_exception": True,
        },
    },
    "std.c::link_available": {
        "item_kind": "operation",
        "source_signature": "std.c::link_available(constexpr str logical_name) -> bool",
        "implementation": {
            "kind": "source",
            "source": "library/std/c/source/link_available.c",
            "c_symbol": "r_std_c_link_available",
            "generator_contract": (
                "Generated C17 passes the current program's immutable resolved logical-link "
                "manifest as a hidden first ABI argument; the public R operation retains its "
                "single constexpr-str parameter."
            ),
        },
    },
}

STD_JSON_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    **{
        f"std.json::{name}": {
            "item_kind": "public_type",
            "implementation": {
                "kind": "header", "header": "library/std/json/include/r_std_json_reader.h",
                "c_type": c_type, "generic_arity": 1,
                "type_glue": {
                    "move_initialize": f"r_std_json_{name}_move_initialize",
                    "drop": f"r_std_json_{name}_destroy", "linkage": "static_inline",
                },
            },
        }
        for name, c_type in (("reader", "RStdJsonReader"), ("detached", "RStdJsonDetached"))
    },
    "std.json::decoder": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header", "header": "library/std/json/include/r_std_json.h",
            "c_type": "RStdJsonDecoder", "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_json_decoder_move_initialize",
                "drop": "r_std_json_decoder_destroy", "linkage": "static_inline",
            },
        },
    },
    **{
        f"std.json::{name}": {
            "item_kind": "public_type",
            "implementation": {
                "kind": "header", "header": "library/std/json/include/r_std_json.h",
                "c_type": c_type,
                "type_glue": {
                    "move_initialize": f"r_std_json_{name}_move_initialize",
                    "drop": f"r_std_json_{name}_destroy", "linkage": "static_inline",
                },
            },
        }
        for name, c_type in (("value", "RStdJsonValue"), ("number", "RStdJsonNumber"),
                             ("error", "RStdJsonError"))
    },
    **{
        f"std.json::{name}": {
            "item_kind": "public_type",
            "implementation": {
                "kind": "header", "header": "library/std/json/include/r_std_json.h",
                "c_type": c_type,
            },
        }
        for name, c_type in (("error_code", "RStdJsonErrorCode"), ("value_kind", "RStdJsonKind"),
                             ("options", "RStdJsonOptions"), ("mode", "RStdJsonMode"),
                             ("feed_state", "RStdJsonDecoderStatus"),
                             ("feed_result", "RStdJsonProgress"))
    },
}


STD_JSON_IMPLEMENTATIONS.update({
    "std.json::parse": {
        "item_kind": "operation", "source_signature": 'std.json::parse(const u8[] source) -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/parse.c",
                           "c_symbol": "r_std_json_parse", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::parse_with_options": {
        "item_kind": "operation", "source_signature": 'std.json::parse_with_options(const u8[] source, std.json::options options) -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/parse_with_options.c",
                           "c_symbol": "r_std_json_parse_with_options", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::stringify": {
        "item_kind": "operation", "source_signature": 'std.json::stringify(const std.json::value* source) -> std.string::string throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/stringify.c",
                           "c_symbol": "r_std_json_stringify", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::stringify_with_options": {
        "item_kind": "operation", "source_signature": 'std.json::stringify_with_options(const std.json::value* source, std.json::options options) -> std.string::string throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/stringify_with_options.c",
                           "c_symbol": "r_std_json_stringify_with_options", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::kind": {
        "item_kind": "operation", "source_signature": 'std.json::kind(const std.json::value* source) -> std.json::value_kind',
        "implementation": {"kind": "source", "source": "library/std/json/source/kind.c",
                           "c_symbol": "r_std_json_kind", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::len": {
        "item_kind": "operation", "source_signature": 'std.json::len(const std.json::value* source) -> usize',
        "implementation": {"kind": "source", "source": "library/std/json/source/len.c",
                           "c_symbol": "r_std_json_len", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::get": {
        "item_kind": "operation", "source_signature": 'std.json::get(const std.json::value* source, usize index) -> o<const std.json::value*>',
        "implementation": {"kind": "source", "source": "library/std/json/source/get.c",
                           "c_symbol": "r_std_json_get", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::find": {
        "item_kind": "operation", "source_signature": 'std.json::find(const std.json::value* source, const u8[] key) -> o<const std.json::value*>',
        "implementation": {"kind": "source", "source": "library/std/json/source/find.c",
                           "c_symbol": "r_std_json_find", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::key_at": {
        "item_kind": "operation", "source_signature": 'std.json::key_at(const std.json::value* source, usize index) -> str',
        "implementation": {"kind": "source", "source": "library/std/json/source/key_at.c",
                           "c_symbol": "r_std_json_key_at", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::boolean": {
        "item_kind": "operation", "source_signature": 'std.json::boolean(const std.json::value* source) -> bool',
        "implementation": {"kind": "source", "source": "library/std/json/source/boolean.c",
                           "c_symbol": "r_std_json_boolean", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::text": {
        "item_kind": "operation", "source_signature": 'std.json::text(const std.json::value* source) -> str',
        "implementation": {"kind": "source", "source": "library/std/json/source/text.c",
                           "c_symbol": "r_std_json_text", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::null": {
        "item_kind": "operation", "source_signature": 'std.json::null() -> std.json::value',
        "implementation": {"kind": "source", "source": "library/std/json/source/null.c",
                           "c_symbol": "r_std_json_null", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::from_bool": {
        "item_kind": "operation", "source_signature": 'std.json::from_bool(bool source) -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/from_bool.c",
                           "c_symbol": "r_std_json_from_bool", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::from_string": {
        "item_kind": "operation", "source_signature": 'std.json::from_string(const u8[] source) -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/from_string.c",
                           "c_symbol": "r_std_json_from_string", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::from_number": {
        "item_kind": "operation", "source_signature": 'std.json::from_number(const std.json::number* source) -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/from_number.c",
                           "c_symbol": "r_std_json_from_number", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::array": {
        "item_kind": "operation", "source_signature": 'std.json::array() -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/array.c",
                           "c_symbol": "r_std_json_array", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::object": {
        "item_kind": "operation", "source_signature": 'std.json::object() -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/object.c",
                           "c_symbol": "r_std_json_object", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::append": {
        "item_kind": "operation", "source_signature": 'std.json::append(std.json::value* target, std.json::value source) -> void throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/append.c",
                           "c_symbol": "r_std_json_append", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::insert": {
        "item_kind": "operation", "source_signature": 'std.json::insert(std.json::value* target, const u8[] key, std.json::value source) -> void throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/insert.c",
                           "c_symbol": "r_std_json_insert", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::parse_number": {
        "item_kind": "operation", "source_signature": 'std.json::parse_number(const u8[] source) -> std.json::number throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/parse_number.c",
                           "c_symbol": "r_std_json_parse_number", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::number_text": {
        "item_kind": "operation", "source_signature": 'std.json::number_text(const std.json::number* source) -> str',
        "implementation": {"kind": "source", "source": "library/std/json/source/number_text.c",
                           "c_symbol": "r_std_json_number_text", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::number_is_zero": {
        "item_kind": "operation", "source_signature": 'std.json::number_is_zero(const std.json::number* source) -> bool',
        "implementation": {"kind": "source", "source": "library/std/json/source/number_is_zero.c",
                           "c_symbol": "r_std_json_number_is_zero", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::name_equal": {
        "item_kind": "operation", "source_signature": 'std.json::name_equal(const u8[] left, const u8[] right, bool ignore_case) -> bool',
        "implementation": {"kind": "source", "source": "library/std/json/source/name_equal.c",
                           "c_symbol": "r_std_json_name_equal", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::take_index": {
        "item_kind": "operation", "source_signature": 'std.json::take_index(std.json::value* target, usize index) -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/take_index.c",
                           "c_symbol": "r_std_json_take_index", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
    "std.json::take_field": {
        "item_kind": "operation", "source_signature": 'std.json::take_field(std.json::value* target, const u8[] key) -> std.json::value throws std.json::error, std.alloc::alloc_error',
        "implementation": {"kind": "source", "source": "library/std/json/source/take_field.c",
                           "c_symbol": "r_std_json_take_field", "conformance_status": "partial",
                           "generator_contract": "Native ownership/scanner ABI; concrete R call adapters are required."},
    },
})

for _json_operation in ['array', 'boolean', 'from_bool', 'from_number', 'from_string', 'kind', 'len', 'null', 'number_is_zero', 'number_text', 'object', 'parse', 'parse_number', 'stringify', 'text']:
    STD_JSON_IMPLEMENTATIONS[f"std.json::{_json_operation}"]["implementation"].pop("conformance_status", None)
    STD_JSON_IMPLEMENTATIONS[f"std.json::{_json_operation}"]["implementation"]["generator_contract"] = (
        "Concrete R call adapters use native checked carriers and call-bounded input borrows."
    )

for _json_operation in ("parse_with_options", "stringify_with_options", "get", "find", "key_at", "append", "insert", "name_equal", "take_index", "take_field"):
    STD_JSON_IMPLEMENTATIONS[f"std.json::{_json_operation}"]["implementation"].pop("conformance_status", None)
for _json_operation in ("marshal", "unmarshal", "marshal_with_options", "unmarshal_with_options"):
    _encode = _json_operation.startswith("marshal")
    _options = ", std.json::options options" if _json_operation.endswith("with_options") else ""
    _source = "const T* source" if _encode else "const u8[] source"
    _result = "std.string::string" if _encode else "T"
    STD_JSON_IMPLEMENTATIONS[f"std.json::{_json_operation}"] = {
        "item_kind": "operation_schema",
        "source_signature": f"std.json::{_json_operation}({_source}{_options}) -> {_result} throws std.json::error, std.alloc::alloc_error",
        "implementation": {
            "kind": "generated",
            "generator_contract": "Concrete sync/async C17 codecs for scalars, strings, arrays, lists, string-key dictionaries, structs, enum/error payloads, optional values, generic JSON constraints and independent hooks/defaults. Embedded structures and object collectors share checked field schemas. Recursive type graphs use nominal back references and runtime nesting limits; typed streaming decoders and asynchronous reader adapters share the checked schema."
        }
    }

# M32.6: the JSON Schema of the automatic conversion of a type (R-SLIB-JSON-0002).
STD_JSON_IMPLEMENTATIONS["std.json::schema"] = {
    "item_kind": "operation_schema",
    "source_signature": "std.json::schema() -> std.json::value throws std.json::error, std.alloc::alloc_error",
    "implementation": {
        "kind": "generated",
        "generator_contract": "The semantic pass derives the JSON Schema 2020-12 text of the closed type argument from the checked conversion schema of marshal and unmarshal (field names, optional, string, embed, descriptions, variants, options, containers, integer ranges, $defs for recursive types); strict C17 parses that text once per call and inserts under $defs the values of the json_schema hooks of types with converters.",
    },
}

for _reader_operation, _reader_signature in (
    ("new_reader", "std.json::new_reader(S source, std.json::options options) -> std.json::reader<S>"),
    ("read_next", "std.json::read_next(std.json::reader<S>* reader, o<std.time::instant> deadline) -> task<o<T> throws std.json::error, std.alloc::alloc_error, TransportError>"),
    ("detach", "std.json::detach(std.json::reader<S> reader) -> std.json::detached<S>"),
    ("take_handle", "std.json::take_handle(std.json::detached<S>* detached) -> S"),
    ("take_bytes", "std.json::take_bytes(std.json::detached<S>* detached) -> bytes"),
):
    STD_JSON_IMPLEMENTATIONS[f"std.json::{_reader_operation}"] = {
        "item_kind": "operation",
        "source_signature": _reader_signature + (" throws std.async::start_error" if _reader_operation == "read_next"
                                                   else " throws std.json::error, std.alloc::alloc_error"),
        "implementation": {
            "kind": "generated", "generator": "compiler/codegen/json_reader.inc",
            "generator_contract": "Concrete transport and checked-result adapters retain typed streaming reader state. Native and generated R tests cover file, pipe input and TCP, transactional start, completion errors, cancellation, EOF and detached owners.",
        },
    }

for _json_operation, _signature in (
    ("new_decoder", "std.json::new_decoder(std.json::options options) -> std.json::decoder<T>"),
    ("feed", "std.json::feed(std.json::decoder<T>* decoder, const u8[] fragment, bool final) -> std.json::feed_result"),
    ("take", "std.json::take(std.json::decoder<T>* decoder) -> T"),
):
    STD_JSON_IMPLEMENTATIONS[f"std.json::{_json_operation}"] = {
        "item_kind": "operation_schema",
        "source_signature": _signature + " throws std.json::error, std.alloc::alloc_error",
        "implementation": {
            "kind": "generated",
            "generator_contract": "Concrete scalar, aggregate, container, variant and hook continuation frames share incremental scanner, transactional take and partial cleanup without a preliminary DOM.",
        },
    }

STD_FORMAT_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.format::builder": {
        "item_kind": "public_type",
        "source_signature": "std.format::builder",
        "implementation": {
            "kind": "header",
            "header": "library/std/format/include/r_std_format.h",
            "c_type": "RStdFormatBuilder",
            "type_glue": {
                "move_initialize": "r_std_format_builder_move_initialize",
                "drop": "r_std_format_builder_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.format::format": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "generated",
            "generator_contract": (
                "Core R-EXPR-0028 creates a concrete local capture schema and typed "
                "rendering helpers for each format literal. Templates are Move-only "
                "and have no exported or uniform C ABI."
            ),
        },
    },
    "std.string::string": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/string/include/r_std_string.h",
            "c_type": "RStdString",
            "type_glue": {
                "move_initialize": "r_std_string_move_initialize",
                "drop": "r_std_string_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.string::from_bytes_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/string/include/r_std_string.h",
            "c_type": "RStdStringFromBytesResult",
            "type_glue": {
                "move_initialize": "r_std_string_from_bytes_result_move_initialize",
                "drop": "r_std_string_from_bytes_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    **{
        f"std.format::append_{suffix}": {
            "item_kind": "operation",
            "source_signature": (
                f"std.format::append_{suffix}(std.format::builder* target, {suffix} value) -> "
                "void throws std.format::format_error"
            ),
            "implementation": {
                "kind": "source",
                "source": f"library/std/format/source/append_{suffix}.c",
                "c_symbol": f"r_std_format_append_{suffix}",
            },
        }
        for suffix in ("f32", "f64", "c_float", "c_double", "c_long_double")
    },
}

# Exact native asynchronous std.time operation mappings. Their public R call result is the
# compiler-defined async start result; the listed C functions are the target runtime bridge.
STD_TIME_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.time::sleep_for": {
        "item_kind": "operation",
        "source_signature": (
            "async std.time::sleep_for(std.time::duration duration) -> "
            "void throws std.time::time_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/time/source/sleep_for.c",
            "c_symbol": "r_std_time_sleep_for",
        },
    },
    "std.time::sleep_until": {
        "item_kind": "operation",
        "source_signature": (
            "async std.time::sleep_until(std.time::instant when) -> "
            "void throws std.time::time_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/time/source/sleep_until.c",
            "c_symbol": "r_std_time_sleep_until",
        },
    },
}

# Exact handwritten std.thread mappings. Generic spawn/join operations use the single type-erased
# C implementation named here; compiler-generated wrappers supply concrete R payload metadata.
STD_THREAD_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.thread::clone_thread": {
        "item_kind": "operation",
        "source_signature": (
            "std.thread::clone_thread(const std.thread::thread* source) -> "
            "std.thread::thread"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/clone_thread.c",
            "c_symbol": "r_std_thread_clone_thread",
        },
    },
    "std.thread::current": {
        "item_kind": "operation",
        "source_signature": "std.thread::current() -> std.thread::thread",
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/current.c",
            "c_symbol": "r_std_thread_current",
        },
    },
    "std.thread::detach": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.thread::detach(move std.thread::join_handle<R throws E...> handle) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/detach.c",
            "c_symbol": "r_std_thread_detach",
        },
    },
    "std.thread::join": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.thread::join(move std.thread::join_handle<R throws E...> handle) -> "
            "std.thread::join_result<R> throws E..."
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/join.c",
            "c_symbol": "r_std_thread_join",
        },
    },
    "std.thread::join_handle": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/thread/include/r_std_thread.h",
            "c_type": "RStdThreadJoinHandle",
        },
    },
    "std.thread::join_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/thread/include/r_std_thread.h",
            "c_type": "RStdThreadJoinResult",
        },
    },
    "std.thread::panic_category": {
        "item_kind": "operation",
        "source_signature": (
            "std.thread::panic_category(const std.thread::panic_report* report) -> "
            "constexpr str"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/panic_category.c",
            "c_symbol": "r_std_thread_panic_category",
        },
    },
    "std.thread::panic_report": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/thread/include/r_std_thread.h",
            "c_type": "RStdThreadPanicReport",
            "type_glue": {
                "move_initialize": "r_std_thread_panic_report_move_initialize",
                "drop": "r_std_thread_panic_report_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.thread::panic_text": {
        "item_kind": "operation",
        "source_signature": (
            "std.thread::panic_text(const std.thread::panic_report* report) -> str"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/panic_text.c",
            "c_symbol": "r_std_thread_panic_text",
        },
    },
    "std.thread::park": {
        "item_kind": "operation",
        "source_signature": "std.thread::park() -> void",
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/park.c",
            "c_symbol": "r_std_thread_park",
        },
    },
    "std.thread::scoped_join_handle": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/thread/include/r_std_thread.h",
            "c_type": "RStdThreadScopedJoinHandle",
        },
    },
    "std.thread::sleep_nanoseconds": {
        "item_kind": "operation",
        "source_signature": "std.thread::sleep_nanoseconds(u64 nanoseconds) -> void",
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/sleep_nanoseconds.c",
            "c_symbol": "r_std_thread_sleep_nanoseconds",
        },
    },
    "std.thread::spawn": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.thread::spawn(entry, arguments...) -> "
            "std.thread::join_handle<R throws E...> throws std.thread::thread_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/spawn.c",
            "c_symbol": "r_std_thread_spawn",
        },
    },
    "std.thread::spawn_scoped": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.thread::spawn_scoped(entry, arguments...) -> "
            "std.thread::scoped_join_handle<R throws E...> throws std.thread::thread_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/spawn_scoped.c",
            "c_symbol": "r_std_thread_spawn_scoped",
        },
    },
    "std.thread::thread": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/thread/include/r_std_thread.h",
            "c_type": "RStdThread",
            "type_glue": {
                "move_initialize": "r_std_thread_thread_move_initialize",
                "drop": "r_std_thread_thread_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.thread::thread_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/thread/include/r_std_thread.h",
            "c_type": "RStdThreadError",
        },
    },
    "std.thread::unpark": {
        "item_kind": "operation",
        "source_signature": (
            "std.thread::unpark(const std.thread::thread* target) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/unpark.c",
            "c_symbol": "r_std_thread_unpark",
        },
    },
    "std.thread::yield_now": {
        "item_kind": "operation",
        "source_signature": "std.thread::yield_now() -> void",
        "implementation": {
            "kind": "source",
            "source": "library/std/thread/source/yield_now.c",
            "c_symbol": "r_std_thread_yield_now",
        },
    },
}

# Exact type-erased std.sync lock mappings. Compiler-generated monomorphic wrappers supply the
# concrete T metadata, aligned value storage, hidden guard region and capability derivation.
# L30 (R-SLIB-ASYNC-0013..0016): asynchronous locks, semaphore, notify and broadcast.
STD_ASYNC_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.async::acquire": {
        "item_kind": "operation",
        "source_signature": (
            "async std.async::acquire(const std.async::semaphore* semaphore) -> "
            "std.async::semaphore_permit"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/acquire.c",
            "c_symbol": "r_std_async_acquire",
        },
    },
    "std.async::add_permits": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::add_permits(const std.async::semaphore* semaphore, usize "
            "count) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/add_permits.c",
            "c_symbol": "r_std_async_add_permits",
        },
    },
    "std.async::available_permits": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::available_permits(const std.async::semaphore* semaphore) -> "
            "usize"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/available_permits.c",
            "c_symbol": "r_std_async_available_permits",
        },
    },
    "std.async::broadcast_receive": {
        "item_kind": "operation_schema",
        "source_signature": (
            "async std.async::broadcast_receive(const "
            "std.async::broadcast_receiver<T>* receiver) -> "
            "std.async::broadcast_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/broadcast_receive.c",
            "c_symbol": "r_std_async_broadcast_receive",
        },
    },
    "std.async::broadcast_receiver": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncBroadcastReceiver",
            "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_async_broadcast_receiver_move_initialize",
                "drop": "r_std_async_broadcast_receiver_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::broadcast_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncBroadcastResult",
            "generic_arity": 1,
        },
    },
    "std.async::clone_broadcast": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::clone_broadcast(const std.async::broadcast<T>* sender) -> "
            "std.async::broadcast<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/clone_broadcast.c",
            "c_symbol": "r_std_async_clone_broadcast",
        },
    },
    "std.async::clone_mutex": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::clone_mutex(const std.async::mutex<T>* mutex) -> "
            "std.async::mutex<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/clone_mutex.c",
            "c_symbol": "r_std_async_clone_mutex",
        },
    },
    "std.async::clone_notify": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::clone_notify(const std.async::notify* notify) -> "
            "std.async::notify"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/clone_notify.c",
            "c_symbol": "r_std_async_clone_notify",
        },
    },
    "std.async::clone_rw_lock": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::clone_rw_lock(const std.async::rw_lock<T>* lock) -> "
            "std.async::rw_lock<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/clone_rw_lock.c",
            "c_symbol": "r_std_async_clone_rw_lock",
        },
    },
    "std.async::clone_semaphore": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::clone_semaphore(const std.async::semaphore* semaphore) -> "
            "std.async::semaphore"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/clone_semaphore.c",
            "c_symbol": "r_std_async_clone_semaphore",
        },
    },
    "std.async::lock": {
        "item_kind": "operation_schema",
        "source_signature": (
            "async std.async::lock(const std.async::mutex<T>* mutex) -> "
            "std.async::mutex_guard<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/lock.c",
            "c_symbol": "r_std_async_lock",
        },
    },
    "std.async::mutex": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncMutex",
            "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_async_mutex_move_initialize",
                "drop": "r_std_async_mutex_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::mutex_guard": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncMutexGuard",
            "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_async_mutex_guard_move_initialize",
                "drop": "r_std_async_mutex_guard_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::mutex_guard_mut": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::mutex_guard_mut(std.async::mutex_guard<T>* guard) -> T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/mutex_guard_mut.c",
            "c_symbol": "r_std_async_mutex_guard_mut",
        },
    },
    "std.async::mutex_guard_ref": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::mutex_guard_ref(const std.async::mutex_guard<T>* guard) -> "
            "const T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/mutex_guard_ref.c",
            "c_symbol": "r_std_async_mutex_guard_ref",
        },
    },
    "std.async::mutex_new": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::mutex_new(T value) -> std.async::mutex<T> throws "
            "std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/mutex_new.c",
            "c_symbol": "r_std_async_mutex_new",
        },
    },
    "std.async::notified": {
        "item_kind": "operation",
        "source_signature": (
            "async std.async::notified(const std.async::notify* notify) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/notified.c",
            "c_symbol": "r_std_async_notified",
        },
    },
    "std.async::notify": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncNotify",
            "type_glue": {
                "move_initialize": "r_std_async_notify_move_initialize",
                "drop": "r_std_async_notify_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::notify_all": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::notify_all(const std.async::notify* notify) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/notify_all.c",
            "c_symbol": "r_std_async_notify_all",
        },
    },
    "std.async::notify_new": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::notify_new() -> std.async::notify throws "
            "std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/notify_new.c",
            "c_symbol": "r_std_async_notify_new",
        },
    },
    "std.async::notify_one": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::notify_one(const std.async::notify* notify) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/notify_one.c",
            "c_symbol": "r_std_async_notify_one",
        },
    },
    "std.async::publish": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::publish(const std.async::broadcast<T>* sender, T value) -> "
            "usize throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/publish.c",
            "c_symbol": "r_std_async_publish",
        },
    },
    "std.async::read": {
        "item_kind": "operation_schema",
        "source_signature": (
            "async std.async::read(const std.async::rw_lock<T>* lock) -> "
            "std.async::rw_read_guard<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/read.c",
            "c_symbol": "r_std_async_read",
        },
    },
    "std.async::release": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::release(std.async::semaphore_permit permit) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/release.c",
            "c_symbol": "r_std_async_release",
        },
    },
    "std.async::rw_lock": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncRwLock",
            "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_async_rw_lock_move_initialize",
                "drop": "r_std_async_rw_lock_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::rw_read_guard": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncRwReadGuard",
            "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_async_rw_read_guard_move_initialize",
                "drop": "r_std_async_rw_read_guard_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::rw_read_guard_ref": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::rw_read_guard_ref(const std.async::rw_read_guard<T>* guard) "
            "-> const T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/rw_read_guard_ref.c",
            "c_symbol": "r_std_async_rw_read_guard_ref",
        },
    },
    "std.async::rw_write_guard": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncRwWriteGuard",
            "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_async_rw_write_guard_move_initialize",
                "drop": "r_std_async_rw_write_guard_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::rw_write_guard_mut": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::rw_write_guard_mut(std.async::rw_write_guard<T>* guard) -> "
            "T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/rw_write_guard_mut.c",
            "c_symbol": "r_std_async_rw_write_guard_mut",
        },
    },
    "std.async::rw_write_guard_ref": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::rw_write_guard_ref(const std.async::rw_write_guard<T>* "
            "guard) -> const T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/rw_write_guard_ref.c",
            "c_symbol": "r_std_async_rw_write_guard_ref",
        },
    },
    "std.async::rwlock_new": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::rwlock_new(T value) -> std.async::rw_lock<T> throws "
            "std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/rwlock_new.c",
            "c_symbol": "r_std_async_rwlock_new",
        },
    },
    "std.async::semaphore": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncSemaphore",
            "type_glue": {
                "move_initialize": "r_std_async_semaphore_move_initialize",
                "drop": "r_std_async_semaphore_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::semaphore_new": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::semaphore_new(usize permits) -> std.async::semaphore throws "
            "std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/semaphore_new.c",
            "c_symbol": "r_std_async_semaphore_new",
        },
    },
    "std.async::semaphore_permit": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/async/include/r_std_async.h",
            "c_type": "RStdAsyncSemaphorePermit",
            "type_glue": {
                "move_initialize": "r_std_async_semaphore_permit_move_initialize",
                "drop": "r_std_async_semaphore_permit_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.async::subscribe": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::subscribe(const std.async::broadcast<T>* sender) -> "
            "std.async::broadcast_receiver<T> throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/subscribe.c",
            "c_symbol": "r_std_async_subscribe",
        },
    },
    "std.async::try_acquire": {
        "item_kind": "operation",
        "source_signature": (
            "std.async::try_acquire(const std.async::semaphore* semaphore) -> "
            "o<std.async::semaphore_permit>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/try_acquire.c",
            "c_symbol": "r_std_async_try_acquire",
        },
    },
    "std.async::try_lock": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::try_lock(const std.async::mutex<T>* mutex) -> "
            "o<std.async::mutex_guard<T>>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/try_lock.c",
            "c_symbol": "r_std_async_try_lock",
        },
    },
    "std.async::try_read": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::try_read(const std.async::rw_lock<T>* lock) -> "
            "o<std.async::rw_read_guard<T>>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/try_read.c",
            "c_symbol": "r_std_async_try_read",
        },
    },
    "std.async::try_write": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::try_write(const std.async::rw_lock<T>* lock) -> "
            "o<std.async::rw_write_guard<T>>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/try_write.c",
            "c_symbol": "r_std_async_try_write",
        },
    },
    "std.async::unlock": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::unlock(G guard) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/unlock.c",
            "c_symbol": "r_std_async_unlock",
        },
    },
    "std.async::write": {
        "item_kind": "operation_schema",
        "source_signature": (
            "async std.async::write(const std.async::rw_lock<T>* lock) -> "
            "std.async::rw_write_guard<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/write.c",
            "c_symbol": "r_std_async_write",
        },
    },
    # L31 (R-SLIB-ASYNC-0017): a direct entry run on the blocking call pool.
    "std.async::task_id": {
        "item_kind": "operation",
        "source_signature": "std.async::task_id() -> u64",
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/task_id.c",
            "c_symbol": "r_std_async_task_id",
        },
    },
    # L39 (R-SLIB-ASYNC-0020): the await of a task that observes its panic.
    "std.async::join": {
        "item_kind": "operation_schema",
        "source_signature": "std.async::join(task<T> operation) -> std.thread::join_result<T>",
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/join.c",
            "c_symbol": "r_std_async_join",
        },
    },
    "std.async::blocking": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.async::blocking(entry, arguments...) -> task<R throws E...> throws "
            "std.async::start_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/blocking.c",
            "c_symbol": "r_std_async_blocking",
        },
    },
    "std.async::broadcast": {
        "item_kind": "type_constructor_schema",
        "source_signature": (
            "std.async::broadcast::<T>(usize capacity) -> std.async::broadcast<T> "
            "throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/async/source/broadcast.c",
            "c_symbol": "r_std_async_broadcast",
        },
    },
}


STD_SYNC_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.sync::barrier": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncBarrier",
            "type_glue": {
                "move_initialize": "r_std_sync_barrier_move_initialize",
                "drop": "r_std_sync_barrier_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.sync::barrier_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncBarrierError",
        },
    },
    "std.sync::barrier_new": {
        "item_kind": "operation",
        "source_signature": (
            "std.sync::barrier_new(usize count) -> "
            "std.sync::barrier throws std.sync::barrier_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/barrier_new.c",
            "c_symbol": "r_std_sync_barrier_new",
        },
    },
    "std.sync::barrier_wait": {
        "item_kind": "operation",
        "source_signature": (
            "std.sync::barrier_wait(const std.sync::barrier* barrier) -> "
            "std.sync::barrier_wait_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/barrier_wait.c",
            "c_symbol": "r_std_sync_barrier_wait",
        },
    },
    "std.sync::barrier_wait_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncBarrierWaitResult",
        },
    },
    "std.sync::call_once": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::call_once(const std.sync::once* once, initializer) -> void throws E..."
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/call_once.c",
            "c_symbol": "r_std_sync_call_once",
        },
    },
    "std.sync::call_once_force": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::call_once_force(const std.sync::once* once, initializer) -> "
            "void throws E..."
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/call_once_force.c",
            "c_symbol": "r_std_sync_call_once_force",
        },
    },
    "std.sync::channel": {
        "item_kind": "type_constructor_schema",
        "source_signature": (
            "std.sync::channel::<T>() -> "
            "std.sync::channel<T> throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/channel.c",
            "c_symbol": "r_std_sync_channel",
        },
    },
    "std.sync::clone_sender": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::clone_sender(const std.sync::sender<T>* source) -> "
            "std.sync::sender<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/clone_sender.c",
            "c_symbol": "r_std_sync_clone_sender",
        },
    },
    "std.sync::clone_sync_sender": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::clone_sync_sender(const std.sync::sync_sender<T>* source) -> "
            "std.sync::sync_sender<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/clone_sync_sender.c",
            "c_symbol": "r_std_sync_clone_sync_sender",
        },
    },
    "std.sync::condvar": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncCondvar",
            "type_glue": {
                "move_initialize": "r_std_sync_condvar_move_initialize",
                "drop": "r_std_sync_condvar_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.sync::condvar_new": {
        "item_kind": "operation",
        "source_signature": "std.sync::condvar_new() -> std.sync::condvar",
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/condvar_new.c",
            "c_symbol": "r_std_sync_condvar_new",
        },
    },
    "std.sync::get": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::get(const std.sync::once_lock<T>* lock) -> o<const T*>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/get.c",
            "c_symbol": "r_std_sync_get",
        },
    },
    "std.sync::get_or_init": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::get_or_init(const std.sync::once_lock<T>* lock, initializer) -> "
            "const T* throws E..."
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/get_or_init.c",
            "c_symbol": "r_std_sync_get_or_init",
        },
    },
    "std.sync::lock": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::lock(const std.sync::mutex<T>* mutex) -> "
            "std.sync::lock_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/lock.c",
            "c_symbol": "r_std_sync_lock",
        },
    },
    "std.sync::lock_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncLockResult",
        },
    },
    "std.sync::mutex": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncMutex",
        },
    },
    "std.sync::mutex_guard": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncMutexGuard",
        },
    },
    "std.sync::mutex_guard_mut": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::mutex_guard_mut(std.sync::mutex_guard<T>* guard) -> T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/mutex_guard_mut.c",
            "c_symbol": "r_std_sync_mutex_guard_mut",
        },
    },
    "std.sync::mutex_guard_ref": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::mutex_guard_ref(const std.sync::mutex_guard<T>* guard) -> const T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/mutex_guard_ref.c",
            "c_symbol": "r_std_sync_mutex_guard_ref",
        },
    },
    "std.sync::mutex_new": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::mutex_new(T value) -> std.sync::mutex<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/mutex_new.c",
            "c_symbol": "r_std_sync_mutex_new",
        },
    },
    "std.sync::notify_all": {
        "item_kind": "operation",
        "source_signature": (
            "std.sync::notify_all(const std.sync::condvar* condition) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/notify_all.c",
            "c_symbol": "r_std_sync_notify_all",
        },
    },
    "std.sync::notify_one": {
        "item_kind": "operation",
        "source_signature": (
            "std.sync::notify_one(const std.sync::condvar* condition) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/notify_one.c",
            "c_symbol": "r_std_sync_notify_one",
        },
    },
    "std.sync::once": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncOnce",
            "type_glue": {
                "move_initialize": "r_std_sync_once_move_initialize",
                "drop": "r_std_sync_once_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.sync::once_lock": {
        "item_kind": "type_constructor_schema",
        "source_signature": "std.sync::once_lock::<T>() -> std.sync::once_lock<T>",
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/once_lock.c",
            "c_symbol": "r_std_sync_once_lock",
        },
    },
    "std.sync::once_new": {
        "item_kind": "operation",
        "source_signature": "std.sync::once_new() -> std.sync::once",
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/once_new.c",
            "c_symbol": "r_std_sync_once_new",
        },
    },
    "std.sync::read": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::read(const std.sync::rw_lock<T>* lock) -> "
            "std.sync::read_lock_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/read.c",
            "c_symbol": "r_std_sync_read",
        },
    },
    "std.sync::receiver": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::receiver(std.sync::channel<T> factory) -> std.sync::receiver<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/receiver.c",
            "c_symbol": "r_std_sync_receiver",
        },
    },
    "std.sync::receive": {
        "item_kind": "operation_schema",
        "source_signature": (
            "async std.sync::receive(const std.sync::receiver<T>* endpoint) -> o<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/receive.c",
            "c_symbol": "r_std_sync_receive",
        },
    },
    "std.sync::recv": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::recv(const std.sync::receiver<T>* endpoint) -> "
            "std.sync::recv_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/recv.c",
            "c_symbol": "r_std_sync_recv",
        },
    },
    "std.sync::recv_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncRecvResult",
        },
    },
    "std.sync::read_lock_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncReadLockResult",
        },
    },
    "std.sync::rw_lock": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncRwLock",
        },
    },
    "std.sync::rw_read_guard": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncRwReadGuard",
        },
    },
    "std.sync::rw_read_guard_ref": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::rw_read_guard_ref(const std.sync::rw_read_guard<T>* guard) -> const T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/rw_read_guard_ref.c",
            "c_symbol": "r_std_sync_rw_read_guard_ref",
        },
    },
    "std.sync::rw_write_guard": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncRwWriteGuard",
        },
    },
    "std.sync::rw_write_guard_mut": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::rw_write_guard_mut(std.sync::rw_write_guard<T>* guard) -> T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/rw_write_guard_mut.c",
            "c_symbol": "r_std_sync_rw_write_guard_mut",
        },
    },
    "std.sync::rw_write_guard_ref": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::rw_write_guard_ref(const std.sync::rw_write_guard<T>* guard) -> const T*"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/rw_write_guard_ref.c",
            "c_symbol": "r_std_sync_rw_write_guard_ref",
        },
    },
    "std.sync::rwlock_new": {
        "item_kind": "operation_schema",
        "source_signature": "std.sync::rwlock_new(T value) -> std.sync::rw_lock<T>",
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/rwlock_new.c",
            "c_symbol": "r_std_sync_rwlock_new",
        },
    },
    "std.sync::set": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::set(const std.sync::once_lock<T>* lock, T value) -> "
            "std.sync::set_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/set.c",
            "c_symbol": "r_std_sync_set",
        },
    },
    "std.sync::set_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncSetResult",
        },
    },
    "std.sync::send": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::send(const std.sync::sender<T>* endpoint, T value) -> "
            "std.sync::send_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/send.c",
            "c_symbol": "r_std_sync_send",
        },
    },
    "std.sync::permit": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncPermit",
            "generic_arity": 1,
            "type_glue": {
                "move_initialize": "r_std_sync_permit_move_initialize",
                "drop": "r_std_sync_permit_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.sync::reserve": {
        "item_kind": "operation_schema",
        "source_signature": (
            "async std.sync::reserve(const std.sync::sync_sender<T>* endpoint) -> "
            "std.sync::reserve_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/reserve.c",
            "c_symbol": "r_std_sync_reserve",
        },
    },
    "std.sync::reserve_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncReserveResult",
        },
    },
    "std.sync::send_permit": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::send_permit(std.sync::permit<T> permit, T value) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/send_permit.c",
            "c_symbol": "r_std_sync_send_permit",
        },
    },
    "std.sync::try_reserve": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::try_reserve(const std.sync::sync_sender<T>* endpoint) -> "
            "std.sync::try_reserve_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/try_reserve.c",
            "c_symbol": "r_std_sync_try_reserve",
        },
    },
    "std.sync::try_reserve_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncTryReserveResult",
        },
    },
    "std.sync::send_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncSendResult",
        },
    },
    "std.sync::sender": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::sender(const (std.sync::channel<T>)* factory) -> std.sync::sender<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/sender.c",
            "c_symbol": "r_std_sync_sender",
        },
    },
    "std.sync::sync_channel": {
        "item_kind": "type_constructor_schema",
        "source_signature": (
            "std.sync::sync_channel::<T>(usize capacity) -> "
            "std.sync::sync_channel<T> throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/sync_channel.c",
            "c_symbol": "r_std_sync_sync_channel",
        },
    },
    "std.sync::sync_receiver": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::sync_receiver(std.sync::sync_channel<T> factory) -> "
            "std.sync::receiver<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/sync_receiver.c",
            "c_symbol": "r_std_sync_sync_receiver",
        },
    },
    "std.sync::sync_send": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::sync_send(const std.sync::sync_sender<T>* endpoint, T value) -> "
            "std.sync::send_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/sync_send.c",
            "c_symbol": "r_std_sync_sync_send",
        },
    },
    "std.sync::sync_sender": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::sync_sender(const (std.sync::sync_channel<T>)* factory) -> "
            "std.sync::sync_sender<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/sync_sender.c",
            "c_symbol": "r_std_sync_sync_sender",
        },
    },
    "std.sync::try_lock": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::try_lock(const std.sync::mutex<T>* mutex) -> "
            "std.sync::try_lock_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/try_lock.c",
            "c_symbol": "r_std_sync_try_lock",
        },
    },
    "std.sync::try_lock_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncTryLockResult",
        },
    },
    "std.sync::try_read": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::try_read(const std.sync::rw_lock<T>* lock) -> "
            "std.sync::try_read_lock_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/try_read.c",
            "c_symbol": "r_std_sync_try_read",
        },
    },
    "std.sync::try_read_lock_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncTryReadLockResult",
        },
    },
    "std.sync::try_recv": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::try_recv(const std.sync::receiver<T>* endpoint) -> "
            "std.sync::try_recv_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/try_recv.c",
            "c_symbol": "r_std_sync_try_recv",
        },
    },
    "std.sync::try_recv_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncTryRecvResult",
        },
    },
    "std.sync::try_send": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::try_send(const std.sync::sync_sender<T>* endpoint, T value) -> "
            "std.sync::try_send_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/try_send.c",
            "c_symbol": "r_std_sync_try_send",
        },
    },
    "std.sync::try_send_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncTrySendResult",
        },
    },
    "std.sync::try_write": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::try_write(const std.sync::rw_lock<T>* lock) -> "
            "std.sync::try_write_lock_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/try_write.c",
            "c_symbol": "r_std_sync_try_write",
        },
    },
    "std.sync::try_write_lock_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncTryWriteLockResult",
        },
    },
    "std.sync::unlock": {
        "item_kind": "operation_schema",
        "source_signature": "std.sync::unlock(G guard) -> void",
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/unlock.c",
            "c_symbol": "r_std_sync_unlock",
        },
    },
    "std.sync::wait": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::wait(const std.sync::condvar* condition, "
            "std.sync::mutex_guard<T> guard) -> std.sync::lock_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/wait.c",
            "c_symbol": "r_std_sync_wait",
        },
    },
    "std.sync::write": {
        "item_kind": "operation_schema",
        "source_signature": (
            "std.sync::write(const std.sync::rw_lock<T>* lock) -> "
            "std.sync::write_lock_result<T>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/sync/source/write.c",
            "c_symbol": "r_std_sync_write",
        },
    },
    "std.sync::write_lock_result": {
        "item_kind": "public_type_schema",
        "implementation": {
            "kind": "header",
            "header": "library/std/sync/include/r_std_sync.h",
            "c_type": "RStdSyncWriteLockResult",
        },
    },
}

# Exact std.fs path/error/type and native-retained async open mappings.
STD_FS_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.fs::access": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsAccess",
        },
    },
    "std.fs::as_error": {
        "item_kind": "operation",
        "source_signature": (
            "std.fs::as_error(std.fs::fs_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/as_error.c",
            "c_symbol": "r_std_fs_as_error",
        },
    },
    "std.fs::create_mode": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsCreateMode",
        },
    },
    "std.fs::create_directory": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::create_directory(const std.fs::path* path, bool recursive, "
            "o<std.time::instant> deadline) -> void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/create_directory.c",
            "c_symbol": "r_std_fs_create_directory",
        },
    },
    "std.fs::create_directory_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::create_directory_beneath(const std.fs::directory* root, "
            "const std.fs::path* relative, bool recursive, o<std.time::instant> deadline) -> "
            "void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/create_directory_beneath.c",
            "c_symbol": "r_std_fs_create_directory_beneath",
        },
    },
    "std.fs::directory": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsDirectory",
            "type_glue": {
                "move_initialize": "r_std_fs_directory_move_initialize",
                "drop": "r_std_fs_directory_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.fs::directory_entry": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsDirectoryEntry",
            "type_glue": {
                "move_initialize": "r_std_fs_directory_entry_move_initialize",
                "drop": "r_std_fs_directory_entry_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.fs::directory_iter": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsDirectoryIter",
            "type_glue": {
                "move_initialize": "r_std_fs_directory_iter_move_initialize",
                "drop": "r_std_fs_directory_iter_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.fs::directory_next_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsDirectoryNextResult",
            "type_glue": {
                "move_initialize": "r_std_fs_directory_next_result_move_initialize",
                "drop": "r_std_fs_directory_next_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.fs::error_code": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsErrorCode",
        },
    },
    "std.fs::fs_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsError",
        },
    },
    "std.fs::file_kind": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsFileKind",
        },
    },
    "std.fs::file": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsFile",
            "type_glue": {
                "move_initialize": "r_std_fs_file_move_initialize",
                "drop": "r_std_fs_file_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.fs::file_metadata": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::file_metadata(const std.fs::file* file, "
            "o<std.time::instant> deadline) -> std.fs::metadata throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/file_metadata.c",
            "c_symbol": "r_std_fs_file_metadata",
        },
    },
    "std.fs::flush": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::flush(const std.fs::file* file, "
            "o<std.time::instant> deadline) -> void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/flush.c",
            "c_symbol": "r_std_fs_flush",
        },
    },
    "std.fs::iterate": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::iterate(const std.fs::directory* directory, "
            "o<std.time::instant> deadline) -> std.fs::directory_iter throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/iterate.c",
            "c_symbol": "r_std_fs_iterate",
        },
    },
    "std.fs::metadata": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsMetadata",
        },
    },
    "std.fs::metadata_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::metadata_beneath(const std.fs::directory* root, "
            "const std.fs::path* relative, o<std.time::instant> deadline) -> "
            "std.fs::metadata throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/metadata_beneath.c",
            "c_symbol": "r_std_fs_metadata_beneath",
        },
    },
    "std.fs::open_file_options": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsOpenFileOptions",
        },
    },
    "std.fs::open_directory": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::open_directory(const std.fs::path* path, "
            "o<std.time::instant> deadline) -> std.fs::directory throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/open_directory.c",
            "c_symbol": "r_std_fs_open_directory",
        },
    },
    "std.fs::open_directory_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::open_directory_beneath(const std.fs::directory* root, "
            "const std.fs::path* relative, o<std.time::instant> deadline) -> "
            "std.fs::directory throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/open_directory_beneath.c",
            "c_symbol": "r_std_fs_open_directory_beneath",
        },
    },
    "std.fs::open_file": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::open_file(const std.fs::path* path, "
            "std.fs::open_file_options options, o<std.time::instant> deadline) -> "
            "std.fs::file throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/open_file.c",
            "c_symbol": "r_std_fs_open_file",
        },
    },
    "std.fs::open_file_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::open_file_beneath(const std.fs::directory* root, "
            "const std.fs::path* relative, std.fs::open_file_options options, "
            "o<std.time::instant> deadline) -> std.fs::file throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/open_file_beneath.c",
            "c_symbol": "r_std_fs_open_file_beneath",
        },
    },
    "std.fs::path": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsPath",
            "type_glue": {
                "move_initialize": "r_std_fs_path_move_initialize",
                "drop": "r_std_fs_path_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.fs::path_clone": {
        "item_kind": "operation",
        "source_signature": (
            "std.fs::path_clone(const std.fs::path* source) -> "
            "std.fs::path throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/path_clone.c",
            "c_symbol": "r_std_fs_path_clone",
        },
    },
    "std.fs::path_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsPathError",
        },
    },
    "std.fs::path_from_utf8": {
        "item_kind": "operation",
        "source_signature": (
            "std.fs::path_from_utf8(str text) -> std.fs::path throws std.fs::path_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/path_from_utf8.c",
            "c_symbol": "r_std_fs_path_from_utf8",
        },
    },
    "std.fs::path_from_utf8_bytes": {
        "item_kind": "operation",
        "source_signature": (
            "std.fs::path_from_utf8_bytes(const u8[] bytes) -> "
            "std.fs::path throws std.fs::path_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/path_from_utf8_bytes.c",
            "c_symbol": "r_std_fs_path_from_utf8_bytes",
        },
    },
    "std.fs::path_is_absolute": {
        "item_kind": "operation",
        "source_signature": (
            "std.fs::path_is_absolute(const std.fs::path* source) -> bool"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/path_is_absolute.c",
            "c_symbol": "r_std_fs_path_is_absolute",
        },
    },
    "std.fs::path_join": {
        "item_kind": "operation",
        "source_signature": (
            "std.fs::path_join(const std.fs::path* base, const std.fs::path* component) -> "
            "std.fs::path throws std.fs::path_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/path_join.c",
            "c_symbol": "r_std_fs_path_join",
        },
    },
    "std.fs::path_to_utf8": {
        "item_kind": "operation",
        "source_signature": (
            "std.fs::path_to_utf8(const std.fs::path* source) -> "
            "std.string::string throws std.fs::path_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/path_to_utf8.c",
            "c_symbol": "r_std_fs_path_to_utf8",
        },
    },
    "std.fs::read_into": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::read_into(const std.fs::file* file, u8[] target, "
            "o<std.time::instant> deadline) -> usize throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/read_into.c",
            "c_symbol": "r_std_fs_read_into",
        },
    },
    "std.fs::write_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write_from(const std.fs::file* file, const u8[] source, "
            "o<std.time::instant> deadline) -> usize throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write_from.c",
            "c_symbol": "r_std_fs_write_from",
        },
    },
    "std.fs::write_all_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write_all_from(const std.fs::file* file, const u8[] source, "
            "o<std.time::instant> deadline) -> void throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write_all_from.c",
            "c_symbol": "r_std_fs_write_all_from",
        },
    },
    "std.fs::read": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::read(const std.fs::file* file, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::read_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/read.c",
            "c_symbol": "r_std_fs_read",
        },
    },
    "std.fs::read_file": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::read_file(const std.fs::path* path, usize limit, "
            "o<std.time::instant> deadline) -> array<u8> throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/read_file.c",
            "c_symbol": "r_std_fs_read_file",
        },
    },
    "std.fs::read_file_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::read_file_beneath(const std.fs::directory* root, "
            "const std.fs::path* relative, usize limit, o<std.time::instant> deadline) -> "
            "array<u8> throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/read_file_beneath.c",
            "c_symbol": "r_std_fs_read_file_beneath",
        },
    },
    "std.fs::next": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::next(std.fs::directory_iter iterator, "
            "o<std.time::instant> deadline) -> std.fs::directory_next_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/next.c",
            "c_symbol": "r_std_fs_next",
        },
    },
    "std.fs::seek_origin": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsSeekOrigin",
        },
    },
    "std.fs::lock_kind": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsLockKind",
        },
    },
    "std.fs::try_lock": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::try_lock(const std.fs::file* file, std.fs::lock_kind kind, u64 start, "
            "u64 length, o<std.time::instant> deadline) -> bool throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/try_lock.c",
            "c_symbol": "r_std_fs_try_lock",
        },
    },
    "std.fs::lock": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::lock(const std.fs::file* file, std.fs::lock_kind kind, u64 start, "
            "u64 length, o<std.time::instant> deadline) -> void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/lock.c",
            "c_symbol": "r_std_fs_lock",
        },
    },
    "std.fs::unlock": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::unlock(const std.fs::file* file, u64 start, u64 length, "
            "o<std.time::instant> deadline) -> void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/unlock.c",
            "c_symbol": "r_std_fs_unlock",
        },
    },
    "std.fs::sync_level": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsSyncLevel",
        },
    },
    "std.fs::read_at": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::read_at(const std.fs::file* file, u64 offset, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::read_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/read_at.c",
            "c_symbol": "r_std_fs_read_at",
        },
    },
    "std.fs::write_all_at": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write_all_at(const std.fs::file* file, u64 offset, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::write_all_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write_all_at.c",
            "c_symbol": "r_std_fs_write_all_at",
        },
    },
    "std.fs::read_at_into": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::read_at_into(const std.fs::file* file, u64 offset, u8[] target, "
            "o<std.time::instant> deadline) -> usize throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/read_at_into.c",
            "c_symbol": "r_std_fs_read_at_into",
        },
    },
    "std.fs::write_all_at_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write_all_at_from(const std.fs::file* file, u64 offset, "
            "const u8[] source, o<std.time::instant> deadline) -> void throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write_all_at_from.c",
            "c_symbol": "r_std_fs_write_all_at_from",
        },
    },
    "std.fs::sync": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::sync(const std.fs::file* file, std.fs::sync_level level, "
            "o<std.time::instant> deadline) -> void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/sync.c",
            "c_symbol": "r_std_fs_sync",
        },
    },
    "std.fs::remove_directory_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::remove_directory_beneath(const std.fs::directory* root, "
            "const std.fs::path* relative, o<std.time::instant> deadline) -> "
            "void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/remove_directory_beneath.c",
            "c_symbol": "r_std_fs_remove_directory_beneath",
        },
    },
    "std.fs::remove_file_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::remove_file_beneath(const std.fs::directory* root, "
            "const std.fs::path* relative, o<std.time::instant> deadline) -> "
            "void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/remove_file_beneath.c",
            "c_symbol": "r_std_fs_remove_file_beneath",
        },
    },
    "std.fs::rename_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::rename_beneath(const std.fs::directory* source_root, "
            "const std.fs::path* source, const std.fs::directory* destination_root, "
            "const std.fs::path* destination, o<std.time::instant> deadline) -> "
            "void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/rename_beneath.c",
            "c_symbol": "r_std_fs_rename_beneath",
        },
    },
    "std.fs::seek": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::seek(const std.fs::file* file, std.fs::seek_origin origin, "
            "i64 offset, o<std.time::instant> deadline) -> u64 throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/seek.c",
            "c_symbol": "r_std_fs_seek",
        },
    },
    "std.fs::write": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write(const std.fs::file* file, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::write_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write.c",
            "c_symbol": "r_std_fs_write",
        },
    },
    "std.fs::write_all": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write_all(const std.fs::file* file, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::write_all_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write_all.c",
            "c_symbol": "r_std_fs_write_all",
        },
    },
    "std.fs::write_file_atomic_no_replace": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write_file_atomic_no_replace(const std.fs::path* path, "
            "array<u8> data, o<std.time::instant> deadline) -> std.fs::write_file_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write_file_atomic_no_replace.c",
            "c_symbol": "r_std_fs_write_file_atomic_no_replace",
        },
    },
    "std.fs::write_file_atomic_no_replace_beneath": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::write_file_atomic_no_replace_beneath("
            "const std.fs::directory* root, const std.fs::path* relative, array<u8> data, "
            "o<std.time::instant> deadline) -> std.fs::write_file_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/write_file_atomic_no_replace_beneath.c",
            "c_symbol": "r_std_fs_write_file_atomic_no_replace_beneath",
        },
    },
    "std.fs::write_file_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/fs/include/r_std_fs.h",
            "c_type": "RStdFsWriteFileResult",
            "type_glue": {
                "move_initialize": "r_std_fs_write_file_result_move_initialize",
                "drop": "r_std_fs_write_file_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.fs::close_directory": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::close_directory(std.fs::directory directory, "
            "o<std.time::instant> deadline) -> void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/close_directory.c",
            "c_symbol": "r_std_fs_close_directory",
        },
    },
    "std.fs::close_file": {
        "item_kind": "operation",
        "source_signature": (
            "async std.fs::close_file(std.fs::file file, "
            "o<std.time::instant> deadline) -> void throws std.fs::fs_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/fs/source/close_file.c",
            "c_symbol": "r_std_fs_close_file",
        },
    },
}

STD_ERROR_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.error::fault": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "generated",
            "generator_contract": (
                "Core R-AGG-0011 closes the family of the standard errors of the generated main "
                "boundary when the program is built: a tagged value holds one of them inline, "
                "without heap storage, and has no runtime symbol."
            ),
        },
    },
    "std.error::from_fault": {
        "item_kind": "operation",
        "source_signature": (
            "std.error::from_fault(std.error::fault value) -> std.error::error"
        ),
        "implementation": {
            "kind": "generated",
            "generator": "compiler/codegen/hosted_main.inc",
            "generator_contract": (
                "Strict C17 lowering selects the held error by its tag and forms the portable "
                "error with the mapping of the generated main boundary; it calls no runtime "
                "symbol, allocates nothing and cannot panic."
            ),
        },
    },
    "std.error::from_address": {
        "item_kind": "operation",
        "source_signature": (
            "std.error::from_address(std.net::address_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/error/source/from_address.c",
            "c_symbol": "r_std_error_from_address",
        },
    },
    "std.error::from_async": {
        "item_kind": "operation",
        "source_signature": (
            "std.error::from_async(std.async::start_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/error/source/from_async.c",
            "c_symbol": "r_std_error_from_async",
        },
    },
    "std.error::from_barrier": {
        "item_kind": "operation",
        "source_signature": (
            "std.error::from_barrier(std.sync::barrier_error value) -> "
            "std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/error/source/from_barrier.c",
            "c_symbol": "r_std_error_from_barrier",
        },
    },
    "std.error::from_path": {
        "item_kind": "operation",
        "source_signature": (
            "std.error::from_path(std.fs::path_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/error/source/from_path.c",
            "c_symbol": "r_std_error_from_path",
        },
    },
    "std.error::from_thread": {
        "item_kind": "operation",
        "source_signature": (
            "std.error::from_thread(std.thread::thread_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/error/source/from_thread.c",
            "c_symbol": "r_std_error_from_thread",
        },
    },
}

STD_NET_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.net::address_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetAddressError",
        },
    },
    "std.net::address_error_code": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetAddressErrorCode",
        },
    },
    "std.net::as_error": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::as_error(std.net::net_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/as_error.c",
            "c_symbol": "r_std_net_as_error",
        },
    },
    "std.net::error_code": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetErrorCode",
        },
    },
    "std.net::family": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetFamily",
        },
    },
    "std.net::format_ip": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::format_ip(std.net::ip_address address) -> "
            "std.string::string throws std.alloc::alloc_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/format_ip.c",
            "c_symbol": "r_std_net_format_ip",
        },
    },
    "std.net::ip_address": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetIpAddress",
        },
    },
    "std.net::listen_options": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetListenOptions",
        },
    },
    "std.net::net_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetError",
        },
    },
    "std.net::parse_ip": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::parse_ip(str text) -> "
            "std.net::ip_address throws std.net::address_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/parse_ip.c",
            "c_symbol": "r_std_net_parse_ip",
        },
    },
    "std.net::resolve": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::resolve(str host, u16 port, std.net::family family, "
            "o<std.time::instant> deadline) -> "
            "array<std.net::socket_address> throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/resolve.c",
            "c_symbol": "r_std_net_resolve",
        },
    },
    "std.net::shutdown_direction": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetShutdownDirection",
        },
    },
    "std.net::socket_address": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetSocketAddress",
        },
    },
    "std.net::tcp_connection": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetTcpConnection",
            "type_glue": {
                "move_initialize": "r_std_net_tcp_connection_move_initialize",
                "drop": "r_std_net_tcp_connection_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::tcp_accept": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_accept(const std.net::tcp_listener* listener, "
            "o<std.time::instant> deadline) -> "
            "std.net::tcp_connection throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_accept.c",
            "c_symbol": "r_std_net_tcp_accept",
        },
    },
    "std.net::tcp_close": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_close(std.net::tcp_stream stream, "
            "o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_close.c",
            "c_symbol": "r_std_net_tcp_close",
        },
    },
    "std.net::tcp_listener_close": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_listener_close(std.net::tcp_listener listener, "
            "o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_listener_close.c",
            "c_symbol": "r_std_net_tcp_listener_close",
        },
    },
    "std.net::tcp_connect": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_connect(std.net::socket_address remote, "
            "o<std.time::instant> deadline) -> "
            "std.net::tcp_stream throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_connect.c",
            "c_symbol": "r_std_net_tcp_connect",
        },
    },
    "std.net::tcp_listen": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_listen(std.net::socket_address local, "
            "std.net::listen_options options, o<std.time::instant> deadline) -> "
            "std.net::tcp_listener throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_listen.c",
            "c_symbol": "r_std_net_tcp_listen",
        },
    },
    "std.net::tcp_listener": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetTcpListener",
            "type_glue": {
                "move_initialize": "r_std_net_tcp_listener_move_initialize",
                "drop": "r_std_net_tcp_listener_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::tcp_listener_local_address": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::tcp_listener_local_address(const std.net::tcp_listener* listener) -> "
            "std.net::socket_address throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_listener_local_address.c",
            "c_symbol": "r_std_net_tcp_listener_local_address",
        },
    },
    "std.net::tcp_get_options": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::tcp_get_options(const std.net::tcp_stream* stream) -> "
            "std.net::tcp_options throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_get_options.c",
            "c_symbol": "r_std_net_tcp_get_options",
        },
    },
    "std.net::tcp_set_options": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::tcp_set_options(const std.net::tcp_stream* stream, std.net::tcp_options options) -> "
            "void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_set_options.c",
            "c_symbol": "r_std_net_tcp_set_options",
        },
    },
    "std.net::udp_get_options": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::udp_get_options(const std.net::udp_socket* socket) -> "
            "std.net::udp_options throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_get_options.c",
            "c_symbol": "r_std_net_udp_get_options",
        },
    },
    "std.net::udp_set_options": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::udp_set_options(const std.net::udp_socket* socket, std.net::udp_options options) -> "
            "void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_set_options.c",
            "c_symbol": "r_std_net_udp_set_options",
        },
    },
    "std.net::udp_join_multicast": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::udp_join_multicast(const std.net::udp_socket* socket, std.net::ip_address group, u32 interface_index) -> "
            "void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_join_multicast.c",
            "c_symbol": "r_std_net_udp_join_multicast",
        },
    },
    "std.net::udp_leave_multicast": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::udp_leave_multicast(const std.net::udp_socket* socket, std.net::ip_address group, u32 interface_index) -> "
            "void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_leave_multicast.c",
            "c_symbol": "r_std_net_udp_leave_multicast",
        },
    },
    "std.net::tcp_options": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetTcpOptions",
        },
    },
    "std.net::udp_options": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUdpOptions",
        },
    },
    "std.net::tcp_local_address": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::tcp_local_address(const std.net::tcp_stream* stream) -> "
            "std.net::socket_address throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_local_address.c",
            "c_symbol": "r_std_net_tcp_local_address",
        },
    },
    "std.net::tcp_peer_address": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::tcp_peer_address(const std.net::tcp_stream* stream) -> "
            "std.net::socket_address throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_peer_address.c",
            "c_symbol": "r_std_net_tcp_peer_address",
        },
    },
    "std.net::tcp_read": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_read(const std.net::tcp_stream* stream, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.net::tcp_read_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_read.c",
            "c_symbol": "r_std_net_tcp_read",
        },
    },
    "std.net::tcp_read_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetTcpReadResult",
            "type_glue": {
                "move_initialize": "r_std_net_tcp_read_result_move_initialize",
                "drop": "r_std_net_tcp_read_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::tcp_shutdown": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_shutdown(const std.net::tcp_stream* stream, "
            "std.net::shutdown_direction direction, o<std.time::instant> deadline) -> "
            "void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_shutdown.c",
            "c_symbol": "r_std_net_tcp_shutdown",
        },
    },
    "std.net::tcp_stream": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetTcpStream",
            "type_glue": {
                "move_initialize": "r_std_net_tcp_stream_move_initialize",
                "drop": "r_std_net_tcp_stream_destroy", "linkage": "static_inline",
            },
        },
    },
    "std.net::tcp_write": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_write(const std.net::tcp_stream* stream, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.net::tcp_write_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_write.c",
            "c_symbol": "r_std_net_tcp_write",
        },
    },
    "std.net::tcp_write_all": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_write_all(const std.net::tcp_stream* stream, "
            "array<u8> buffer, o<std.time::instant> deadline) -> "
            "std.net::tcp_write_all_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_write_all.c",
            "c_symbol": "r_std_net_tcp_write_all",
        },
    },
    "std.net::tcp_write_all_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetTcpWriteAllResult",
            "type_glue": {
                "move_initialize": "r_std_net_tcp_write_all_result_move_initialize",
                "drop": "r_std_net_tcp_write_all_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::tcp_write_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetTcpWriteResult",
            "type_glue": {
                "move_initialize": "r_std_net_tcp_write_result_move_initialize",
                "drop": "r_std_net_tcp_write_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::udp_bind": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::udp_bind(std.net::socket_address local, bool reuse_address, "
            "o<std.time::instant> deadline) -> std.net::udp_socket throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_bind.c",
            "c_symbol": "r_std_net_udp_bind",
        },
    },
    "std.net::udp_close": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::udp_close(std.net::udp_socket socket, "
            "o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_close.c",
            "c_symbol": "r_std_net_udp_close",
        },
    },
    "std.net::udp_local_address": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::udp_local_address(const std.net::udp_socket* socket) -> "
            "std.net::socket_address throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_local_address.c",
            "c_symbol": "r_std_net_udp_local_address",
        },
    },
    "std.net::tcp_read_into": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_read_into(const std.net::tcp_stream* stream, u8[] target, "
            "o<std.time::instant> deadline) -> usize throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_read_into.c",
            "c_symbol": "r_std_net_tcp_read_into",
        },
    },
    "std.net::tcp_write_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_write_from(const std.net::tcp_stream* stream, const u8[] source, "
            "o<std.time::instant> deadline) -> usize throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_write_from.c",
            "c_symbol": "r_std_net_tcp_write_from",
        },
    },
    "std.net::tcp_write_all_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::tcp_write_all_from(const std.net::tcp_stream* stream, "
            "const u8[] source, o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/tcp_write_all_from.c",
            "c_symbol": "r_std_net_tcp_write_all_from",
        },
    },
    "std.net::udp_send_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::udp_send_from(const std.net::udp_socket* socket, "
            "std.net::socket_address peer, const u8[] source, o<std.time::instant> deadline) -> "
            "void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_send_from.c",
            "c_symbol": "r_std_net_udp_send_from",
        },
    },
    "std.net::udp_receive_into": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::udp_receive_into(const std.net::udp_socket* socket, u8[] target, "
            "o<std.time::instant> deadline) -> std.net::datagram throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_receive_into.c",
            "c_symbol": "r_std_net_udp_receive_into",
        },
    },
    "std.net::unix_listener": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUnixListener",
            "type_glue": {
                "move_initialize": "r_std_net_unix_listener_move_initialize",
                "drop": "r_std_net_unix_listener_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::unix_stream": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUnixStream",
            "type_glue": {
                "move_initialize": "r_std_net_unix_stream_move_initialize",
                "drop": "r_std_net_unix_stream_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::unix_datagram": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUnixDatagram",
            "type_glue": {
                "move_initialize": "r_std_net_unix_datagram_move_initialize",
                "drop": "r_std_net_unix_datagram_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::peer_credentials": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetPeerCredentials",
        },
    },
    "std.net::unix_message": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUnixMessage",
        },
    },
    "std.net::unix_listen": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_listen(str path, u32 backlog, bool replace, o<std.time::instant> deadline) -> std.net::unix_listener throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_listen.c",
            "c_symbol": "r_std_net_unix_listen",
        },
    },
    "std.net::unix_accept": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_accept(const std.net::unix_listener* listener, o<std.time::instant> deadline) -> std.net::unix_stream throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_accept.c",
            "c_symbol": "r_std_net_unix_accept",
        },
    },
    "std.net::unix_connect": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_connect(str path, o<std.time::instant> deadline) -> std.net::unix_stream throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_connect.c",
            "c_symbol": "r_std_net_unix_connect",
        },
    },
    "std.net::unix_listener_close": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_listener_close(std.net::unix_listener listener, o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_listener_close.c",
            "c_symbol": "r_std_net_unix_listener_close",
        },
    },
    "std.net::unix_read_into": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_read_into(const std.net::unix_stream* stream, u8[] target, o<std.time::instant> deadline) -> usize throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_read_into.c",
            "c_symbol": "r_std_net_unix_read_into",
        },
    },
    "std.net::unix_write_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_write_from(const std.net::unix_stream* stream, const u8[] source, o<std.time::instant> deadline) -> usize throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_write_from.c",
            "c_symbol": "r_std_net_unix_write_from",
        },
    },
    "std.net::unix_write_all_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_write_all_from(const std.net::unix_stream* stream, const u8[] source, o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_write_all_from.c",
            "c_symbol": "r_std_net_unix_write_all_from",
        },
    },
    "std.net::unix_shutdown": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_shutdown(const std.net::unix_stream* stream, std.net::shutdown_direction direction, o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_shutdown.c",
            "c_symbol": "r_std_net_unix_shutdown",
        },
    },
    "std.net::unix_close": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_close(std.net::unix_stream stream, o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_close.c",
            "c_symbol": "r_std_net_unix_close",
        },
    },
    "std.net::unix_peer_credentials": {
        "item_kind": "operation",
        "source_signature": (
            "std.net::unix_peer_credentials(const std.net::unix_stream* stream) -> std.net::peer_credentials throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_peer_credentials.c",
            "c_symbol": "r_std_net_unix_peer_credentials",
        },
    },
    "std.net::unix_datagram_bind": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_datagram_bind(str path, bool replace, o<std.time::instant> deadline) -> std.net::unix_datagram throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_datagram_bind.c",
            "c_symbol": "r_std_net_unix_datagram_bind",
        },
    },
    "std.net::unix_datagram_connect": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_datagram_connect(str path, o<std.time::instant> deadline) -> std.net::unix_datagram throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_datagram_connect.c",
            "c_symbol": "r_std_net_unix_datagram_connect",
        },
    },
    "std.net::unix_send_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_send_from(const std.net::unix_datagram* socket, const u8[] source, o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_send_from.c",
            "c_symbol": "r_std_net_unix_send_from",
        },
    },
    "std.net::unix_receive_into": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_receive_into(const std.net::unix_datagram* socket, u8[] target, o<std.time::instant> deadline) -> std.net::unix_message throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_receive_into.c",
            "c_symbol": "r_std_net_unix_receive_into",
        },
    },
    "std.net::unix_datagram_close": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::unix_datagram_close(std.net::unix_datagram socket, o<std.time::instant> deadline) -> void throws std.net::net_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/unix_datagram_close.c",
            "c_symbol": "r_std_net_unix_datagram_close",
        },
    },
    "std.net::datagram": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetDatagram",
        },
    },
    "std.net::udp_receive_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::udp_receive_from(const std.net::udp_socket* socket, "
            "array<u8> buffer, o<std.time::instant> deadline) -> "
            "std.net::udp_receive_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_receive_from.c",
            "c_symbol": "r_std_net_udp_receive_from",
        },
    },
    "std.net::udp_receive_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUdpReceiveResult",
            "type_glue": {
                "move_initialize": "r_std_net_udp_receive_result_move_initialize",
                "drop": "r_std_net_udp_receive_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::udp_send_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUdpSendResult",
            "type_glue": {
                "move_initialize": "r_std_net_udp_send_result_move_initialize",
                "drop": "r_std_net_udp_send_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.net::udp_send_to": {
        "item_kind": "operation",
        "source_signature": (
            "async std.net::udp_send_to(const std.net::udp_socket* socket, "
            "std.net::socket_address peer, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.net::udp_send_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/net/source/udp_send_to.c",
            "c_symbol": "r_std_net_udp_send_to",
        },
    },
    "std.net::udp_socket": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/net/include/r_std_net.h",
            "c_type": "RStdNetUdpSocket",
            "type_glue": {
                "move_initialize": "r_std_net_udp_socket_move_initialize",
                "drop": "r_std_net_udp_socket_destroy",
                "linkage": "static_inline",
            },
        },
    },
}

STD_PROCESS_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.process::abort": {
        "item_kind": "operation",
        "source_signature": "std.process::abort() -> never",
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/abort.c",
            "c_symbol": "r_std_process_abort",
        },
    },
    "std.process::arg": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::arg(std.process::command* command, str value) -> "
            "void throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/arg.c",
            "c_symbol": "r_std_process_arg",
        },
    },
    "std.process::as_error": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::as_error(std.process::process_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/as_error.c",
            "c_symbol": "r_std_process_as_error",
        },
    },
    "std.process::clear_environment": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::clear_environment(std.process::command* command) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/clear_environment.c",
            "c_symbol": "r_std_process_clear_environment",
        },
    },
    "std.process::child": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessChild",
            "type_glue": {
                "move_initialize": "r_std_process_child_move_initialize",
                "drop": "r_std_process_child_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.process::command": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessCommand",
            "type_glue": {
                "move_initialize": "r_std_process_command_move_initialize",
                "drop": "r_std_process_command_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.process::command_create": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::command_create(const std.fs::path* executable) -> "
            "std.process::command throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/command_create.c",
            "c_symbol": "r_std_process_command_create",
        },
    },
    "std.process::environment": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::environment(std.process::command* command, str name, str value) -> "
            "void throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/environment.c",
            "c_symbol": "r_std_process_environment",
        },
    },
    "std.process::error_code": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessErrorCode",
        },
    },
    "std.process::exit_status": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessExitStatus",
        },
    },
    "std.process::pipe_mode": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessPipeMode",
        },
    },
    "std.process::process_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessError",
        },
    },
    "std.process::remove_environment": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::remove_environment(std.process::command* command, str name) -> "
            "void throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/remove_environment.c",
            "c_symbol": "r_std_process_remove_environment",
        },
    },
    "std.process::set_stdio": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::set_stdio(std.process::command* command, "
            "std.process::stdio policy) -> void"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/set_stdio.c",
            "c_symbol": "r_std_process_set_stdio",
        },
    },
    "std.process::spawn": {
        "item_kind": "operation",
        "source_signature": (
            "async std.process::spawn(std.process::command command, "
            "o<std.time::instant> deadline) -> std.process::spawn_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/spawn.c",
            "c_symbol": "r_std_process_spawn",
        },
    },
    "std.process::spawn_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessSpawnResult",
            "type_glue": {
                "move_initialize": "r_std_process_spawn_result_move_initialize",
                "drop": "r_std_process_spawn_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.process::take_stderr": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::take_stderr(std.process::child* child) -> o<std.io::input>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/take_stderr.c",
            "c_symbol": "r_std_process_take_stderr",
        },
    },
    "std.process::take_stdin": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::take_stdin(std.process::child* child) -> o<std.io::output>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/take_stdin.c",
            "c_symbol": "r_std_process_take_stdin",
        },
    },
    "std.process::take_stdout": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::take_stdout(std.process::child* child) -> o<std.io::input>"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/take_stdout.c",
            "c_symbol": "r_std_process_take_stdout",
        },
    },
    "std.process::terminate": {
        "item_kind": "operation",
        "source_signature": (
            "async std.process::terminate(const std.process::child* child, "
            "o<std.time::instant> deadline) -> void throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/terminate.c",
            "c_symbol": "r_std_process_terminate",
        },
    },
    "std.process::stdio": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessStdio",
        },
    },
    "std.process::termination_kind": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessTerminationKind",
        },
    },
    "std.process::wait": {
        "item_kind": "operation",
        "source_signature": (
            "async std.process::wait(std.process::child child, "
            "o<std.time::instant> deadline) -> std.process::wait_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/wait.c",
            "c_symbol": "r_std_process_wait",
        },
    },
    "std.process::wait_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/process/include/r_std_process.h",
            "c_type": "RStdProcessWaitResult",
            "type_glue": {
                "move_initialize": "r_std_process_wait_result_move_initialize",
                "drop": "r_std_process_wait_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.process::working_directory": {
        "item_kind": "operation",
        "source_signature": (
            "std.process::working_directory(std.process::command* command, "
            "const std.fs::path* path) -> void throws std.process::process_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/process/source/working_directory.c",
            "c_symbol": "r_std_process_working_directory",
        },
    },
}

STD_IO_IMPLEMENTATIONS: dict[str, dict[str, Any]] = {
    "std.io::as_error": {
        "item_kind": "operation",
        "source_signature": (
            "std.io::as_error(std.io::io_error value) -> std.error::error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/as_error.c",
            "c_symbol": "r_std_io_as_error",
        },
    },
    "std.io::close_input": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::close_input(std.io::input stream, "
            "o<std.time::instant> deadline) -> void throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/close_input.c",
            "c_symbol": "r_std_io_close_input",
        },
    },
    "std.io::close_output": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::close_output(std.io::output stream, "
            "o<std.time::instant> deadline) -> void throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/close_output.c",
            "c_symbol": "r_std_io_close_output",
        },
    },
    "std.io::error_code": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoErrorCode",
        },
    },
    "std.io::flush": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::flush(const std.io::output* stream, "
            "o<std.time::instant> deadline) -> void throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/flush.c",
            "c_symbol": "r_std_io_flush",
        },
    },
    "std.io::input": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoInput",
            "type_glue": {
                "move_initialize": "r_std_io_input_move_initialize",
                "drop": "r_std_io_input_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.io::io_error": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoError",
        },
    },
    "std.io::output": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoOutput",
            "type_glue": {
                "move_initialize": "r_std_io_output_move_initialize",
                "drop": "r_std_io_output_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.io::read_into": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::read_into(const std.io::input* stream, u8[] target, "
            "o<std.time::instant> deadline) -> usize throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/read_into.c",
            "c_symbol": "r_std_io_read_into",
        },
    },
    "std.io::write_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::write_from(const std.io::output* stream, const u8[] source, "
            "o<std.time::instant> deadline) -> usize throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/write_from.c",
            "c_symbol": "r_std_io_write_from",
        },
    },
    "std.io::write_all_from": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::write_all_from(const std.io::output* stream, const u8[] source, "
            "o<std.time::instant> deadline) -> void throws std.io::io_error"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/write_all_from.c",
            "c_symbol": "r_std_io_write_all_from",
        },
    },
    "std.io::read": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::read(const std.io::input* stream, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::read_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/read.c",
            "c_symbol": "r_std_io_read",
        },
    },
    "std.io::read_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoReadResult",
            "type_glue": {
                "move_initialize": "r_std_io_read_result_move_initialize",
                "drop": "r_std_io_read_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.io::shared_write_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoSharedWriteResult",
            "type_glue": {
                "move_initialize": "r_std_io_shared_write_result_move_initialize",
                "drop": "r_std_io_shared_write_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.io::stderr": {
        "item_kind": "operation",
        "source_signature": "std.io::stderr() -> std.io::output",
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/stderr.c",
            "c_symbol": "r_std_io_stderr",
        },
    },
    "std.io::stdin": {
        "item_kind": "operation",
        "source_signature": "std.io::stdin() -> std.io::input",
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/stdin.c",
            "c_symbol": "r_std_io_stdin",
        },
    },
    "std.io::stdout": {
        "item_kind": "operation",
        "source_signature": "std.io::stdout() -> std.io::output",
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/stdout.c",
            "c_symbol": "r_std_io_stdout",
        },
    },
    "std.io::write": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::write(const std.io::output* stream, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::write_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/write.c",
            "c_symbol": "r_std_io_write",
        },
    },
    "std.io::write_all": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::write_all(const std.io::output* stream, array<u8> buffer, "
            "o<std.time::instant> deadline) -> std.io::write_all_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/write_all.c",
            "c_symbol": "r_std_io_write_all",
        },
    },
    "std.io::write_all_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoWriteAllResult",
            "type_glue": {
                "move_initialize": "r_std_io_write_all_result_move_initialize",
                "drop": "r_std_io_write_all_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
    "std.io::write_shared": {
        "item_kind": "operation",
        "source_signature": (
            "async std.io::write_shared(const std.io::output* stream, "
            "arc (array<u8>) buffer, usize offset, usize length, "
            "o<std.time::instant> deadline) -> std.io::shared_write_result"
        ),
        "implementation": {
            "kind": "source",
            "source": "library/std/io/source/write_shared.c",
            "c_symbol": "r_std_io_write_shared",
        },
    },
    "std.io::write_result": {
        "item_kind": "public_type",
        "implementation": {
            "kind": "header",
            "header": "library/std/io/include/r_std_io.h",
            "c_type": "RStdIoWriteResult",
            "type_glue": {
                "move_initialize": "r_std_io_write_result_move_initialize",
                "drop": "r_std_io_write_result_destroy",
                "linkage": "static_inline",
            },
        },
    },
}

def sync_type_constructor_collision(
    item_id: str,
    c_type: str,
    operation_rules: tuple[str, ...],
    type_rules: tuple[str, ...],
    move_initialize: str | None,
    drop: str | None,
) -> dict[str, dict[str, Any]]:
    operation = STD_SYNC_IMPLEMENTATIONS[item_id]
    type_implementation: dict[str, Any] = {
        "kind": "header",
        "header": "library/std/sync/include/r_std_sync.h",
        "c_type": c_type,
        "generic_arity": 1,
    }
    if move_initialize is not None and drop is not None:
        type_implementation["type_glue"] = {
            "move_initialize": move_initialize,
            "drop": drop,
            "linkage": "static_inline",
        }
    return {
        "operation": {
            "item_kind": operation["item_kind"],
            "normative_rules": operation_rules,
            "source_signature": operation["source_signature"],
            "implementation": operation["implementation"],
        },
        "type": {
            "item_kind": "public_type_schema",
            "normative_rules": type_rules,
            "implementation": type_implementation,
        },
    }


# R permits a type and an operation to have the same source spelling. Inventory identity therefore
# includes a semantic facet; the spelling itself remains unchanged. These are the closed collisions
# present in Standard Library 0.1. Each facet has its own normative evidence and implementation
# record so a type declaration can never accidentally satisfy an operation implementation gate.
COLLIDING_ITEM_FACETS: dict[str, dict[str, dict[str, Any]]] = {
    "std.async::broadcast": {
        "operation": {
            "item_kind": STD_ASYNC_IMPLEMENTATIONS["std.async::broadcast"]["item_kind"],
            "normative_rules": ("R-SLIB-ASYNC-0016",),
            "source_signature": STD_ASYNC_IMPLEMENTATIONS["std.async::broadcast"][
                "source_signature"
            ],
            "implementation": STD_ASYNC_IMPLEMENTATIONS["std.async::broadcast"]["implementation"],
        },
        "type": {
            "item_kind": "public_type_schema",
            "normative_rules": ("R-SLIB-ASYNC-0016", "R-LIB-0018"),
            "implementation": {
                "kind": "header",
                "header": "library/std/async/include/r_std_async.h",
                "c_type": "RStdAsyncBroadcast",
                "generic_arity": 1,
                "type_glue": {
                    "move_initialize": "r_std_async_broadcast_move_initialize",
                    "drop": "r_std_async_broadcast_destroy",
                    "linkage": "static_inline",
                },
            },
        },
    },
    "std.sync::channel": sync_type_constructor_collision(
        "std.sync::channel",
        "RStdSyncChannel",
        ("R-LIB-0016",),
        ("R-LIB-0016", "R-LIB-0018"),
        "r_std_sync_channel_move_initialize",
        "r_std_sync_channel_destroy",
    ),
    "std.sync::once_lock": sync_type_constructor_collision(
        "std.sync::once_lock",
        "RStdSyncOnceLock",
        ("R-LIB-0015",),
        ("R-LIB-0015", "R-LIB-0018"),
        None,
        None,
    ),
    "std.sync::receiver": sync_type_constructor_collision(
        "std.sync::receiver",
        "RStdSyncReceiver",
        ("R-LIB-0016",),
        ("R-LIB-0016", "R-SLIB-SERVICE-0003", "R-SLIB-SERVICE-0007", "R-SLIB-HTTP-0007", "R-SLIB-HTTP-0014",
         "R-SLIB-HTTP-0015", "R-SLIB-MCP-0021"),
        "r_std_sync_receiver_move_initialize",
        "r_std_sync_receiver_destroy",
    ),
    "std.sync::sender": sync_type_constructor_collision(
        "std.sync::sender",
        "RStdSyncSender",
        ("R-LIB-0016",),
        ("R-LIB-0016",),
        "r_std_sync_sender_move_initialize",
        "r_std_sync_sender_destroy",
    ),
    "std.sync::sync_channel": sync_type_constructor_collision(
        "std.sync::sync_channel",
        "RStdSyncSyncChannel",
        ("R-LIB-0016",),
        ("R-LIB-0016", "R-LIB-0018"),
        "r_std_sync_sync_channel_move_initialize",
        "r_std_sync_sync_channel_destroy",
    ),
    "std.sync::sync_sender": sync_type_constructor_collision(
        "std.sync::sync_sender",
        "RStdSyncSyncSender",
        ("R-LIB-0016",),
        ("R-LIB-0016",),
        "r_std_sync_sync_sender_move_initialize",
        "r_std_sync_sync_sender_destroy",
    ),
    "std.dict::iter": {
        "operation": {
            "item_kind": "operation_schema",
            "normative_rules": ("R-LIB-0020",),
            "source_signature": (
                "std.dict::iter(const dict<K,V>* source) -> std.dict::iter<K,V>"
            ),
            "implementation": {
                "kind": "source",
                "source": "library/std/dict/source/iter.c",
                "c_symbol": "r_std_dict_iter",
            },
        },
        "type": {
            "item_kind": "public_type_schema",
            "normative_rules": ("R-LIB-0020", "R-LIB-0021"),
            "implementation": {
                "kind": "header",
                "header": "library/std/dict/include/r_std_dict.h",
                "c_type": "RStdDictIterator",
            },
        },
    },
    "std.fs::metadata": {
        "operation": {
            "item_kind": "operation",
            "normative_rules": ("R-SLIB-FS-0006",),
            "source_signature": (
                "async std.fs::metadata(const std.fs::path* path, "
                "o<std.time::instant> deadline) -> "
                "std.fs::metadata throws std.fs::fs_error"
            ),
            "implementation": {
                "kind": "source",
                "source": "library/std/fs/source/metadata.c",
                "c_symbol": "r_std_fs_metadata",
            },
        },
        "type": {
            "item_kind": "public_type",
            "normative_rules": ("R-SLIB-FS-0006",),
            "implementation": {
                "kind": "header",
                "header": "library/std/fs/include/r_std_fs.h",
                "c_type": "RStdFsMetadata",
            },
        },
    },
    "std.list::iter": {
        "operation": {
            "item_kind": "operation_schema",
            "normative_rules": ("R-LIB-0022",),
            "source_signature": (
                "std.list::iter(const list<T>* source) -> std.list::iter<T>"
            ),
            "implementation": {
                "kind": "source",
                "source": "library/std/list/source/iter.c",
                "c_symbol": "r_std_list_iter",
            },
        },
        "type": {
            "item_kind": "public_type_schema",
            "normative_rules": ("R-LIB-0022", "R-LIB-0023"),
            "implementation": {
                "kind": "header",
                "header": "library/std/list/include/r_std_list.h",
                "c_type": "RStdListIterator",
            },
        },
    },
}

UNQUALIFIED_SIGNATURE_EXEMPTIONS = {
    ("R-SLIB-TERM-0004", "async module::operation(P...) -> T throws E..."),
    ("R-SLIB-C-0003", "raw fn(raw void*) -> void"),
}

# Library section 21 documents the methods of an R-source type and the callable constraints of
# its adapters inside the rule of the type or operation that owns them; those unqualified
# signatures are contracts of the qualified items above them, never public items of their own.
UNQUALIFIED_SIGNATURE_RULE_EXEMPTIONS = frozenset(
    (
        "R-SLIB-CBOR-0001",
        "R-SLIB-CBOR-0002",
        "R-SLIB-CBOR-0003",
        "R-SLIB-CBOR-0004",
        "R-SLIB-CBOR-0005",
        "R-SLIB-CBOR-0006",
        "R-SLIB-CRYPTO-0001",
        "R-SLIB-CRYPTO-0002",
        "R-SLIB-CRYPTO-0003",
        "R-SLIB-CRYPTO-0004",
        "R-SLIB-CRYPTO-0005",
        "R-SLIB-CRYPTO-0006",
        "R-SLIB-CRYPTO-0007",
        "R-SLIB-CRYPTO-0008",
        "R-SLIB-CRYPTO-0009",
        "R-SLIB-CRYPTO-0010",
        "R-SLIB-CRYPTO-0011",
        "R-SLIB-CRYPTO-0012",
        "R-SLIB-JWT-0001",
        "R-SLIB-JWT-0002",
        "R-SLIB-JWT-0003",
        "R-SLIB-JWT-0004",
        "R-SLIB-JWT-0005",
        "R-SLIB-JWT-0006",
        "R-SLIB-JWT-0007",
        "R-SLIB-OAUTH2-0001",
        "R-SLIB-OAUTH2-0002",
        "R-SLIB-OAUTH2-0003",
        "R-SLIB-OAUTH2-0004",
        "R-SLIB-OAUTH2-0005",
        "R-SLIB-OAUTH2-0006",
        "R-SLIB-COSE-0001",
        "R-SLIB-COSE-0002",
        "R-SLIB-COSE-0003",
        "R-SLIB-COSE-0004",
        "R-SLIB-COSE-0005",
        "R-SLIB-COSE-0006",
        "R-SLIB-SQLITE-0001",
        "R-SLIB-SQLITE-0002",
        "R-SLIB-SQLITE-0003",
        "R-SLIB-SQLITE-0004",
        "R-SLIB-SQLITE-0005",
        "R-SLIB-SQLITE-0006",
        "R-SLIB-SQLITE-0007",
        "R-SLIB-SQLITE-0008",
        "R-SLIB-SERVICE-0004",
        "R-SLIB-SERVICE-0005",
        "R-SLIB-SERVICE-0006",
        "R-SLIB-SERVICE-0007",
        "R-SLIB-HTTP-0013",
        "R-SLIB-HTTP-0014",
        "R-SLIB-HTTP-0015",
        "R-SLIB-HTTP-0016",
        "R-SLIB-HTTP-0017",
        "R-SLIB-HTTP-0018",
        "R-SLIB-METRICS-0001",
        "R-SLIB-METRICS-0002",
        "R-SLIB-METRICS-0003",
        "R-SLIB-METRICS-0004",
        "R-SLIB-TRACE-0001",
        "R-SLIB-TRACE-0002",
        "R-SLIB-TRACE-0003",
        "R-SLIB-ALLOC-0004",
        "R-SLIB-ARENA-0001",
        "R-SLIB-ARENA-0002",
        "R-SLIB-ARENA-0003",
        "R-SLIB-ARENA-0004",
        "R-SLIB-ARENA-0005",
        "R-SLIB-POOL-0001",
        "R-SLIB-POOL-0002",
        "R-SLIB-POOL-0003",
        "R-SLIB-FS-0017",
        "R-SLIB-PG-0001",
        "R-SLIB-PG-0002",
        "R-SLIB-PG-0003",
        "R-SLIB-PG-0004",
        "R-SLIB-PG-0005",
        "R-SLIB-PG-0006",
        "R-SLIB-PG-0007",
        "R-SLIB-PG-0008",
        "R-SLIB-PG-0009",
        "R-SLIB-PG-0010",
        "R-SLIB-PG-0011",
        "R-SLIB-PG-0012",
        "R-SLIB-PG-0013",
        "R-SLIB-PG-0014",
        "R-SLIB-PG-0015",
        "R-SLIB-PG-0016",
        "R-SLIB-CMP-0001",
        "R-SLIB-ITER-0002",
        "R-SLIB-ITER-0003",
        "R-SLIB-SLICE-0002",
        "R-SLIB-SET-0001",
        "R-SLIB-DEQUE-0001",
        "R-SLIB-HEAP-0001",
        "R-SLIB-SORTED-0001",
        "R-SLIB-SORTED-0002",
        "R-SLIB-DEFLATE-0003",
        "R-SLIB-DEFLATE-0004",
        "R-SLIB-XML-0003",
        "R-SLIB-XML-0004",
        "R-SLIB-XML-0005",
        "R-SLIB-XML-0006",
        "R-SLIB-SERVICE-0003",
        "R-SLIB-STREAM-0001",
        "R-SLIB-BUFIO-0001",
        "R-SLIB-BUFIO-0002",
        "R-SLIB-BUFIO-0003",
        "R-SLIB-BUFIO-0004",
        "R-SLIB-LOG-0001",
        "R-SLIB-LOG-0002",
        "R-SLIB-LOG-0003",
        "R-SLIB-LOG-0004",
        "R-SLIB-LOG-0005",
        "R-SLIB-ARGS-0001",
        "R-SLIB-ARGS-0002",
        "R-SLIB-ARGS-0003",
        "R-SLIB-CONFIG-0001",
        "R-SLIB-CONFIG-0002",
        "R-SLIB-CONFIG-0003",
        "R-SLIB-CONFIG-0004",
        "R-SLIB-TEST-0001",
        "R-SLIB-TEST-0002",
        "R-SLIB-BYTES-0009",
        "R-SLIB-ITER-0004",
        "R-SLIB-BYTES-0010",
        "R-SLIB-RANDOM-0002",
        "R-SLIB-TIME-0009",
        "R-SLIB-TIME-0010",
        "R-SLIB-TIME-0011",
        "R-SLIB-TIME-0012",
        "R-SLIB-TIME-0013",
        "R-SLIB-TIME-0014",
        "R-SLIB-JSON-0003",
        "R-SLIB-TLS-0003",
        "R-SLIB-TLS-0004",
        "R-SLIB-URL-0002",
        "R-SLIB-URL-0003",
        "R-SLIB-URL-0004",
        "R-SLIB-URL-0005",
        "R-SLIB-MIME-0001",
        "R-SLIB-MIME-0002",
        "R-SLIB-HTTP-0001",
        "R-SLIB-HTTP-0002",
        "R-SLIB-HTTP-0003",
        "R-SLIB-HTTP-0004",
        "R-SLIB-HTTP-0005",
        "R-SLIB-HTTP-0006",
        "R-SLIB-HTTP-0007",
        "R-SLIB-HTTP-0008",
        "R-SLIB-HTTP-0009",
        "R-SLIB-HTTP-0010",
        "R-SLIB-HTTP-0011",
        "R-SLIB-DNS-0003",
        "R-SLIB-DNS-0004",
        "R-SLIB-DNS-0005",
        "R-SLIB-DNS-0006",
        "R-SLIB-WS-0002",
        "R-SLIB-WS-0003",
        "R-SLIB-WS-0004",
        "R-SLIB-WS-0005",
        "R-SLIB-HTTP-0012",
        "R-SLIB-JSONRPC-0002",
        "R-SLIB-MCP-0002",
        "R-SLIB-MCP-0003",
        "R-SLIB-MCP-0004",
        "R-SLIB-MCP-0005",
        "R-SLIB-MCP-0006",
        "R-SLIB-MCP-0007",
        "R-SLIB-MCP-0008",
        "R-SLIB-MCP-0009",
        "R-SLIB-MCP-0010",
        "R-SLIB-MCP-0011",
        "R-SLIB-MCP-0012",
        "R-SLIB-MCP-0017",
        "R-SLIB-MCP-0018",
        "R-SLIB-MCP-0019",
        "R-SLIB-MCP-0020",
        "R-SLIB-MCP-0021",
        "R-SLIB-MCP-0024",
    )
)


def canonical_json(value: Any) -> str:
    """Return the one committed representation used for generated inventory files."""

    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=False) + "\n"


def module_for_item(item_id: str) -> str:
    return item_id.split("::", maxsplit=1)[0]


def make_record_id(item_id: str, facet: str) -> str:
    if facet not in ITEM_FACETS:
        raise ValueError(f"unknown public-item facet {facet!r} for {item_id}")
    return f"{item_id}#{facet}"


def facet_for_item_kind(item_id: str, item_kind: str) -> str:
    """Return the closed semantic identity facet for one detailed inventory kind."""

    if item_kind in TYPE_ITEM_KINDS:
        return "type"
    if item_kind in OPERATION_ITEM_KINDS:
        return "operation"
    if item_kind in CONSTANT_ITEM_KINDS:
        return "constant"
    if item_kind == "unclassified_public_item":
        if item_id in UNCLASSIFIED_TYPE_ITEMS:
            return "type"
        if item_id in UNCLASSIFIED_OPERATION_ITEMS:
            return "operation"
        raise ValueError(
            f"unclassified public item requires an explicit semantic facet: {item_id}"
        )
    raise ValueError(f"public item {item_id} has unsupported item_kind {item_kind!r}")


def rule_blocks(specification_text: str) -> dict[str, str]:
    matches = list(ANCHOR_PATTERN.finditer(specification_text))
    blocks: dict[str, str] = {}
    for index, match in enumerate(matches):
        rule_id = match.group(1)
        if rule_id in blocks:
            raise ValueError(f"duplicate normative rule anchor: {rule_id}")
        end = matches[index + 1].start() if index + 1 < len(matches) else len(specification_text)
        blocks[rule_id] = specification_text[match.end() : end]
    return blocks


def explicit_items_by_id() -> dict[str, ExplicitPublicItem]:
    result: dict[str, ExplicitPublicItem] = {}
    for family in EXPLICIT_RULE_ITEMS.values():
        for item in family:
            previous = result.get(item.item_id)
            if previous is not None and previous != item:
                raise ValueError(f"conflicting explicit public item: {item.item_id}")
            result[item.item_id] = item
    return result


def validate_explicit_items(specification_text: str) -> None:
    blocks = rule_blocks(specification_text)
    if set(EXPLICIT_RULE_BLOCK_SHA256) != set(EXPLICIT_RULE_ITEMS):
        raise ValueError("explicit public-item rules and rule hashes differ")
    for rule_id, family in EXPLICIT_RULE_ITEMS.items():
        block = blocks.get(rule_id)
        if block is None:
            raise ValueError(f"explicit public-item rule is absent: {rule_id}")
        actual_hash = hashlib.sha256(block.encode("utf-8")).hexdigest()
        if actual_hash != EXPLICIT_RULE_BLOCK_SHA256[rule_id]:
            raise ValueError(
                f"explicit public-item rule changed and requires catalog audit: {rule_id}"
            )
        for item in family:
            if item.evidence not in block:
                raise ValueError(
                    f"explicit public item {item.item_id} has no evidence "
                    f"{item.evidence!r} in {rule_id}"
                )

    explicit_items = explicit_items_by_id()
    for item_id, specialization in SCHEMA_SPECIALIZATIONS.items():
        if specialization.schema_family not in explicit_items:
            raise ValueError(
                f"schema specialization {item_id} names unknown family "
                f"{specialization.schema_family}"
            )
        for rule_id in specialization.normative_rules:
            if rule_id not in blocks:
                raise ValueError(
                    f"schema specialization {item_id} names absent rule {rule_id}"
                )
        if (specialization.source is None) != (specialization.c_symbol is None):
            raise ValueError(
                f"schema specialization {item_id} must define both source and C symbol"
            )
        if item_id not in specification_text:
            if specialization.schema_family.endswith("_S"):
                family_prefix = specialization.schema_family.removesuffix("S")
                allowed_suffixes = MATH_SCALAR_SUFFIXES
            elif specialization.schema_family.endswith("_C"):
                family_prefix = specialization.schema_family.removesuffix("C")
                allowed_suffixes = COMPLEX_SUFFIXES
            else:
                raise ValueError(
                    f"schema specialization has no closed family: {item_id}"
                )
            suffix = item_id.removeprefix(family_prefix)
            if not item_id.startswith(family_prefix) or suffix not in allowed_suffixes:
                raise ValueError(
                    f"schema specialization has no normative derivation: {item_id}"
                )

    qualified_signature = re.compile(
        r"(?:core|std\.[a-z][a-z0-9]*)::[A-Za-z_][A-Za-z0-9_]*\s*(?:::\s*<[^()]*>\s*)?\("
    )
    for rule_id, block in blocks.items():
        for raw_span in re.findall(r"`\+(.*?)\+`", block, re.DOTALL):
            span = " ".join(raw_span.split())
            if "->" not in span or qualified_signature.search(span) is not None:
                continue
            if (rule_id, span) in UNQUALIFIED_SIGNATURE_EXEMPTIONS:
                continue
            if rule_id in UNQUALIFIED_SIGNATURE_RULE_EXEMPTIONS:
                continue
            if rule_id not in EXPLICIT_RULE_ITEMS:
                raise ValueError(
                    f"unqualified public-schema candidate requires explicit audit: "
                    f"{rule_id}: {span}"
                )


# Core traits the library specification names when it describes iterators and formatting (Core
# R-TYPE-0046), and the profile predicate of module `@if` (Core R-META-0002); they are Core
# language items with no library record.
CORE_LANGUAGE_ITEMS = frozenset(
    ("core::Iterator", "core::Contains", "core::CaseMatcher", "core::Format", "core::profile")
)


def collect_normative_items(specification_text: str) -> dict[str, set[str]]:
    """Collect every public module item and the rule contexts in which it is cited."""

    validate_explicit_items(specification_text)
    items: dict[str, set[str]] = {}
    current_anchor: str | None = None
    token_pattern = re.compile(
        rf"{ANCHOR_PATTERN.pattern}|{QUALIFIED_ITEM_PATTERN.pattern}"
    )
    for match in token_pattern.finditer(specification_text):
        anchor = match.group(1)
        item_id = match.group(2)
        if anchor is not None:
            current_anchor = anchor
            continue
        if item_id is None or item_id in CORE_LANGUAGE_ITEMS:
            continue
        rules = items.setdefault(item_id, set())
        if current_anchor is not None:
            rules.add(current_anchor)

    for rule_id, family in EXPLICIT_RULE_ITEMS.items():
        for item in family:
            items.setdefault(item.item_id, set()).add(rule_id)
    for item_id, specialization in SCHEMA_SPECIALIZATIONS.items():
        items.setdefault(item_id, set()).update(specialization.normative_rules)
    blocks = rule_blocks(specification_text)
    for rule_id, item_ids in SUPPLEMENTAL_RULE_ITEM_ATTRIBUTIONS.items():
        if rule_id not in blocks:
            raise ValueError(f"supplemental public-item rule is absent: {rule_id}")
        if len(item_ids) != len(set(item_ids)):
            raise ValueError(f"supplemental public-item rule has duplicate items: {rule_id}")
        for item_id in item_ids:
            if item_id not in items:
                raise ValueError(
                    f"supplemental public item is not otherwise declared: {rule_id}: {item_id}"
                )
            items[item_id].add(rule_id)
    return items


def qualified_callable_signature_item(span: str) -> str | None:
    """Return a qualified callable declared by a complete top-level signature span."""

    match = QUALIFIED_CALLABLE_HEAD_PATTERN.match(span)
    if match is None:
        return None
    depth = 0
    for index in range(match.end() - 1, len(span)):
        character = span[index]
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                remainder = span[index + 1 :].lstrip()
                return match.group(1) if remainder.startswith("->") else None
            if depth < 0:
                return None
    return None


def collect_qualified_callable_signatures(
    specification_text: str,
) -> dict[str, dict[str, set[str]]]:
    """Collect exact qualified callable signatures without classifying result-type mentions."""

    result: dict[str, dict[str, set[str]]] = {}
    for rule_id, block in rule_blocks(specification_text).items():
        for raw_span in re.findall(r"`\+(.*?)\+`", block, re.DOTALL):
            span = " ".join(raw_span.split())
            item_id = qualified_callable_signature_item(span)
            if item_id is None:
                continue
            require_current_source_signature(item_id, span)
            span = normalize_checked_signature(item_id, span)
            signatures = result.setdefault(item_id, {})
            signatures.setdefault(span, set()).add(rule_id)
    return result


def module_records() -> list[dict[str, Any]]:
    records = []
    parts = r_part_modules(repository_root())
    for r_name, target, directory, token in MODULES:
        record: dict[str, Any] = {
            "r_module": r_name,
            "cmake_target": target,
            "directory": directory,
            "descriptor_symbol": f"r_library_internal_module_{token}_descriptor",
            "descriptor_source": (
                "library/internal/diagnostics/source/module_descriptor.c"
            ),
        }
        if r_name in parts:
            # Library R-SLIB-RSRC-0001: the R part is translated with a program importing it.
            record["r_part"] = {"source": parts[r_name][0], "minimum_profile": parts[r_name][1]}
        records.append(record)
    for r_name, (source, profile) in r_source_modules(repository_root()).items():
        # Library R-SLIB-RSRC-0001: an R-source module is translated with the program.
        records.append(
            {
                "r_module": r_name,
                "implementation_language": "r",
                "source": source,
                "minimum_profile": profile,
            }
        )
    return records


def existing_items(
    existing: dict[str, Any] | None,
) -> tuple[dict[str, dict[str, Any]], dict[str, list[dict[str, Any]]]]:
    """Index current and schema-1 records without conflating equal R spellings."""

    by_record_id: dict[str, dict[str, Any]] = {}
    by_spelling: dict[str, list[dict[str, Any]]] = {}
    if existing is None:
        return by_record_id, by_spelling
    items = existing.get("items", [])
    if not isinstance(items, list):
        return by_record_id, by_spelling
    for item in items:
        if not isinstance(item, dict):
            continue
        item_id = item.get("id")
        if not isinstance(item_id, str):
            continue
        by_spelling.setdefault(item_id, []).append(item)

        record_id = item.get("record_id")
        facet = item.get("facet")
        if isinstance(record_id, str) and isinstance(facet, str):
            if facet in ITEM_FACETS and record_id == make_record_id(item_id, facet):
                by_record_id.setdefault(record_id, item)
            continue

        # Schema 1 had one record per spelling. Non-colliding records migrate by semantic kind;
        # combined collision records are rebuilt from COLLIDING_ITEM_FACETS below.
        if item_id in COLLIDING_ITEM_FACETS:
            continue
        item_kind = item.get("item_kind")
        if not isinstance(item_kind, str):
            continue
        try:
            legacy_facet = facet_for_item_kind(item_id, item_kind)
        except ValueError:
            continue
        by_record_id.setdefault(make_record_id(item_id, legacy_facet), item)
    return by_record_id, by_spelling


def canonical_implementation_for(item_id: str) -> dict[str, Any] | None:
    catalogs = (
        ORDINARY_OUTCOME_TYPE_IMPLEMENTATIONS,
        CORE_INTRINSIC_IMPLEMENTATIONS,
        MATH_PARTS_TYPE_IMPLEMENTATIONS,
        MATH_PARTS_SCHEMA_IMPLEMENTATIONS,
        MATH_COMPLEX_TYPE_IMPLEMENTATIONS,
        MATH_COMPLEX_SCHEMA_IMPLEMENTATIONS,
        MATH_NON_FAILING_SCHEMA_IMPLEMENTATIONS,
        MATH_FALLIBLE_SCHEMA_IMPLEMENTATIONS,
        STD_ARRAY_IMPLEMENTATIONS,
        STD_CONVERT_IMPLEMENTATIONS,
        STD_BYTES_IMPLEMENTATIONS,
        STD_HASH_IMPLEMENTATIONS,
        STD_UTF8_IMPLEMENTATIONS,
        STD_BITS_IMPLEMENTATIONS,
        STD_SECRET_IMPLEMENTATIONS,
        STD_RANDOM_IMPLEMENTATIONS,
        STD_TEST_IMPLEMENTATIONS,
        STD_SIGNAL_IMPLEMENTATIONS,
        STD_C_IMPLEMENTATIONS,
        STD_FORMAT_IMPLEMENTATIONS,
        STD_JSON_IMPLEMENTATIONS,
        STD_TIME_IMPLEMENTATIONS,
        STD_THREAD_IMPLEMENTATIONS,
        STD_ASYNC_IMPLEMENTATIONS,
        STD_SYNC_IMPLEMENTATIONS,
        STD_FS_IMPLEMENTATIONS,
        STD_ERROR_IMPLEMENTATIONS,
        STD_NET_IMPLEMENTATIONS,
        STD_PROCESS_IMPLEMENTATIONS,
        STD_IO_IMPLEMENTATIONS,
    )
    for catalog in catalogs:
        implementation = catalog.get(item_id)
        if implementation is not None:
            return implementation
    return None


def existing_classification_item(
    item_id: str,
    candidates: list[dict[str, Any]],
    has_callable_signature: bool,
) -> dict[str, Any]:
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        return {}
    identities = {
        (candidate.get("record_id"), candidate.get("facet"), candidate.get("item_kind"))
        for candidate in candidates
    }
    if len(identities) == 1:
        return candidates[0]

    # A discovered callable may coexist with a previously classified type or constant. Select the
    # non-operation record as the base and let append_discovered_operation_facet rebuild the
    # callable record by its own identity. This keeps generic collisions stable on every rerun.
    if has_callable_signature:
        non_operation = [
            candidate
            for candidate in candidates
            if candidate.get("facet") in {"constant", "type"}
        ]
        if len(non_operation) == 1:
            return non_operation[0]
    raise ValueError(f"public item {item_id} has ambiguous existing facet classification")


def collision_records(
    item_id: str,
    normative_rules: set[str],
    callable_signatures: dict[str, set[str]],
    previous: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    definitions = COLLIDING_ITEM_FACETS[item_id]
    operation_definition = definitions.get("operation")
    if operation_definition is None:
        raise ValueError(f"collision {item_id} has no operation facet")
    operation_signature = operation_definition.get("source_signature")
    if isinstance(operation_signature, str):
        operation_signature = normalize_checked_signature(item_id, operation_signature)
    if callable_signatures:
        if set(callable_signatures) != {operation_signature}:
            raise ValueError(f"collision {item_id} callable signature attribution is stale")
        operation_rules = {
            rule_id for rules in callable_signatures.values() for rule_id in rules
        }
        if operation_rules != set(operation_definition["normative_rules"]):
            raise ValueError(f"collision {item_id} callable rule attribution is stale")
    elif operation_definition.get("item_kind") != "type_constructor_schema":
        raise ValueError(f"collision {item_id} callable signature attribution is missing")
    records: list[dict[str, Any]] = []
    covered_rules: set[str] = set()
    for facet, definition in sorted(definitions.items()):
        if facet not in ITEM_FACETS:
            raise ValueError(f"collision {item_id} has unknown facet {facet}")
        declared_rules = set(definition["normative_rules"])
        if not declared_rules or not declared_rules <= normative_rules:
            raise ValueError(
                f"collision {item_id}#{facet} has invalid normative rule attribution"
            )
        covered_rules.update(declared_rules)
        item_kind = definition["item_kind"]
        if facet_for_item_kind(item_id, item_kind) != facet:
            raise ValueError(f"collision {item_id}#{facet} has mismatched item_kind")
        record_id = make_record_id(item_id, facet)
        old_item = previous.get(record_id, {})
        implementation = definition.get("implementation")
        if implementation is None:
            implementation = old_item.get("implementation", {"kind": "unimplemented"})
        record: dict[str, Any] = {
            "record_id": record_id,
            "id": item_id,
            "facet": facet,
            "module": module_for_item(item_id),
            "item_kind": item_kind,
            "normative_rules": sorted(declared_rules),
            "implementation": implementation,
        }
        source_signature = definition.get("source_signature")
        if isinstance(source_signature, str):
            record["source_signature"] = normalize_item_signature(
                item_id, facet, source_signature
            )
        elif "source_signature" in old_item:
            record["source_signature"] = normalize_item_signature(
                item_id, facet, old_item["source_signature"]
            )
        if "source_signature" in record:
            record["checked_effect"] = checked_effect_contract(
                item_id, record["source_signature"]
            )
        records.append(record)
    if covered_rules != normative_rules:
        raise ValueError(
            f"collision {item_id} facet rules do not cover extracted normative rules"
        )
    return records


def append_discovered_operation_facet(
    item_id: str,
    callable_signatures: dict[str, set[str]],
    previous: dict[str, dict[str, Any]],
    items: list[dict[str, Any]],
) -> None:
    if len(callable_signatures) != 1:
        raise ValueError(
            f"qualified callable {item_id} has multiple exact signatures and requires audit"
        )
    source_signature, rules = next(iter(callable_signatures.items()))
    record_id = make_record_id(item_id, "operation")
    old_item = previous.get(record_id, {})
    items.append(
        {
            "record_id": record_id,
            "id": item_id,
            "facet": "operation",
            "module": module_for_item(item_id),
            "item_kind": "operation",
            "normative_rules": sorted(rules),
            "implementation": old_item.get(
                "implementation", {"kind": "unimplemented"}
            ),
            "source_signature": normalize_checked_signature(item_id, source_signature),
        }
    )
    items[-1]["checked_effect"] = checked_effect_contract(
        item_id, items[-1]["source_signature"]
    )


def standard_pod_types() -> set[str]:
    path = Path(__file__).resolve().parents[1] / "compiler/semantic/standard_pod_types.def"
    result = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r'R_STANDARD_POD\("([a-z0-9_.:]+)"\)', line)
        if match:
            if match[1] in result:
                raise ValueError(f"duplicate POD capability: {match[1]}")
            result.add(match[1])
        elif line.strip() and not line.startswith("/*"):
            raise ValueError(f"invalid POD capability record: {line}")
    return result


# R-SLIB-ERR-0004: the root of the standard errors names their family in throws and catch; it is
# never an exact error of a carrier, so the compiler registry of exact errors omits it.
STANDARD_ERROR_ROOTS = frozenset(("std.error::fault",))


def standard_error_types() -> dict[str, int]:
    registry = Path(__file__).resolve().parents[1] / "compiler/semantic/standard_errors.def"
    errors: dict[str, int] = {}
    for line in registry.read_text().splitlines():
        if not line.strip() or line.startswith("/*"):
            continue
        match = re.fullmatch(r'R_STANDARD_ERROR\("([a-z0-9_.:]+)", ([012])\)', line)
        if match is None or match.group(1) in errors:
            raise ValueError(f"invalid or duplicate standard error registry entry: {line}")
        errors[match.group(1)] = int(match.group(2))
    return errors


def build_inventory(
    specification_path: Path,
    existing: dict[str, Any] | None = None,
) -> dict[str, Any]:
    specification_bytes = specification_path.read_bytes()
    specification_text = specification_bytes.decode("utf-8")
    revision_match = None
    for pattern in REVISION_PATTERNS:
        revision_match = pattern.search(specification_text)
        if revision_match is not None:
            break
    if revision_match is None:
        raise ValueError("specification has no document revision")

    root = repository_root()
    expected_modules = {record[0] for record in MODULES} | set(r_source_modules(root))
    normative_items = collect_normative_items(specification_text)
    callable_signatures = collect_qualified_callable_signatures(specification_text)
    # Source aliases expose existing operation contracts. Keep their complete
    # candidate sets separately instead of inventing a union checked-error set.
    call_style = json.loads((root / "library/standard_methods.json").read_text())
    source_aliases = call_style["overloads"]
    alias_sources = {source for family in source_aliases for source in family["sources"]}
    for family in source_aliases:
        if family["name"] not in alias_sources:
            normative_items.pop(family["name"], None)

    unknown_modules = sorted(
        {module_for_item(item_id) for item_id in normative_items} - expected_modules
    )
    if unknown_modules:
        raise ValueError(
            "specification cites modules missing from MODULES: " + ", ".join(unknown_modules)
        )
    unknown_callables = sorted(set(callable_signatures) - set(normative_items))
    if unknown_callables:
        raise ValueError(
            "qualified callable signatures are missing public-item records: "
            + ", ".join(unknown_callables)
        )

    previous, previous_by_spelling = existing_items(existing)
    explicit_items = explicit_items_by_id()
    items = []
    for item_id in sorted(normative_items):
        if item_id in COLLIDING_ITEM_FACETS:
            items.extend(
                collision_records(
                    item_id,
                    normative_items[item_id],
                    callable_signatures.get(item_id, {}),
                    previous,
                )
            )
            continue

        explicit_item = explicit_items.get(item_id)
        specialization = SCHEMA_SPECIALIZATIONS.get(item_id)
        canonical_implementation = canonical_implementation_for(item_id)
        if canonical_implementation is None:
            canonical_implementation = r_source_implementation_for(root, item_id)
        default_item_kind = (
            specialization.item_kind
            if specialization is not None
            else (
                explicit_item.item_kind
                if explicit_item is not None
                else (
                    "operation"
                    if item_id in callable_signatures
                    else "unclassified_public_item"
                )
            )
        )
        spelling_candidates = previous_by_spelling.get(item_id, [])
        classification_item = existing_classification_item(
            item_id, spelling_candidates, item_id in callable_signatures
        )
        item_kind = (
            canonical_implementation["item_kind"]
            if canonical_implementation is not None
            else (
                default_item_kind
                if specialization is not None or explicit_item is not None
                else classification_item.get("item_kind", default_item_kind)
            )
        )
        facet = facet_for_item_kind(item_id, item_kind)
        record_id = make_record_id(item_id, facet)
        legacy_item = (
            classification_item
            if "record_id" not in classification_item and "facet" not in classification_item
            else {}
        )
        old_item = previous.get(record_id, legacy_item)
        if canonical_implementation is not None:
            implementation = canonical_implementation["implementation"]
        elif specialization is not None and specialization.source is not None:
            implementation = {
                "kind": "source",
                "source": specialization.source,
                "c_symbol": specialization.c_symbol,
            }
        else:
            implementation = old_item.get("implementation", {"kind": "unimplemented"})
        record: dict[str, Any] = {
            "record_id": record_id,
            "id": item_id,
            "facet": facet,
            "module": module_for_item(item_id),
            "item_kind": item_kind,
            "normative_rules": sorted(normative_items[item_id]),
            "implementation": implementation,
        }
        if canonical_implementation is not None and "source_signature" in canonical_implementation:
            record["source_signature"] = normalize_item_signature(
                item_id, facet, canonical_implementation["source_signature"]
            )
        elif (
            item_id in CANONICAL_SOURCE_SIGNATURE_IDS
            and explicit_item is not None
            and explicit_item.source_signature is not None
        ):
            record["source_signature"] = normalize_item_signature(
                item_id, facet, explicit_item.source_signature
            )
        elif "source_signature" in old_item:
            record["source_signature"] = normalize_item_signature(
                item_id, facet, old_item["source_signature"]
            )
        elif specialization is not None and specialization.source_signature is not None:
            record["source_signature"] = normalize_item_signature(
                item_id, facet, specialization.source_signature
            )
        elif explicit_item is not None and explicit_item.source_signature is not None:
            record["source_signature"] = normalize_item_signature(
                item_id, facet, explicit_item.source_signature
            )
        if facet == "operation" and "source_signature" in record:
            record["checked_effect"] = checked_effect_contract(
                item_id, record["source_signature"]
            )
        if specialization is not None:
            record["schema_family"] = specialization.schema_family
        items.append(record)
        if facet == "type" and item_id in callable_signatures:
            append_discovered_operation_facet(
                item_id, callable_signatures[item_id], previous, items
            )
        elif facet == "constant" and item_id in callable_signatures:
            raise ValueError(f"constant {item_id} is also declared as a callable")

    errors = standard_error_types()
    pods = standard_pod_types()
    type_ids = set()
    operation_ids = {item["id"] for item in items if item["facet"] == "operation"}
    unknown_panic_operations = sorted(PANIC_ALLOCATION_OPERATIONS - operation_ids)
    if unknown_panic_operations:
        raise ValueError(
            "panic-allocation operations lack inventory operation facets: "
            + ", ".join(unknown_panic_operations)
        )
    unknown_discardable_operations = sorted(DISCARDABLE_OPERATIONS - operation_ids)
    if unknown_discardable_operations:
        raise ValueError(
            "discardable operations lack inventory operation facets: "
            + ", ".join(unknown_discardable_operations)
        )
    unknown_scoped_operations = sorted(SCOPED_OPERATIONS - operation_ids)
    if unknown_scoped_operations:
        raise ValueError(
            "scoped operations lack inventory operation facets: "
            + ", ".join(unknown_scoped_operations)
        )
    for item in items:
        if item["facet"] == "operation":
            item["panics_on_allocation_failure"] = item["id"] in PANIC_ALLOCATION_OPERATIONS
            item["discardable_result"] = item["id"] in DISCARDABLE_OPERATIONS
            item["scoped"] = item["id"] in SCOPED_OPERATIONS
            if item["discardable_result"] and item["implementation"].get("kind") == "r_source":
                raise ValueError(f"R-source operations declare @discardable in source: {item['id']}")
        if item["facet"] == "type":
            type_ids.add(item["id"])
            item["pod"] = item["id"] in pods
            item["checked_error"] = item["id"] in errors or item["id"] in STANDARD_ERROR_ROOTS
            if item["checked_error"]:
                item["checked_error_arity"] = errors.get(item["id"], 0)
            elif item["implementation"]["kind"] == "r_source":
                source = root / item["implementation"]["source"]
                declaration = r_source_declaration(
                    source.read_text(encoding="utf-8"), item["id"].split("::", 1)[1]
                )
                if declaration is not None and declaration[0] == "error":
                    item["checked_error"] = True
                    item["checked_error_arity"] = declaration[1]
    if pods - type_ids:
        raise ValueError("POD capabilities lack inventory types: " + ", ".join(sorted(pods - type_ids)))
    missing_errors = errors.keys() - type_ids
    if missing_errors:
        raise ValueError("standard errors lack inventory type facets: " + ", ".join(sorted(missing_errors)))

    operation_record_ids = {
        item["record_id"] for item in items if item["facet"] == "operation"
    }
    missing_callable_records = sorted(
        make_record_id(item_id, "operation")
        for item_id in callable_signatures
        if make_record_id(item_id, "operation") not in operation_record_ids
    )
    if missing_callable_records:
        raise ValueError(
            "qualified callable signatures lack operation facets: "
            + ", ".join(missing_callable_records)
        )
    unknown_alias_sources = sorted(alias_sources - {item["id"] for item in items})
    if unknown_alias_sources:
        raise ValueError("source aliases lack operation contracts: " + ", ".join(unknown_alias_sources))
    items.sort(key=lambda item: item["record_id"])

    if specification_path.parent.name == "specification":
        display_path = f"specification/{specification_path.name}"
    else:
        display_path = specification_path.as_posix()

    return {
        "schema_version": 3,
        "generated_from": {
            "path": display_path,
            "revision": revision_match.group(1),
            "sha256": hashlib.sha256(specification_bytes).hexdigest(),
        },
        "extraction": {
            "qualified_item_pattern": QUALIFIED_ITEM_PATTERN.pattern,
            "explicit_rule_item_counts": {
                rule_id: len(EXPLICIT_RULE_ITEMS[rule_id]) for rule_id in sorted(EXPLICIT_RULE_ITEMS)
            },
            "explicit_rule_block_sha256": {
                rule_id: EXPLICIT_RULE_BLOCK_SHA256[rule_id]
                for rule_id in sorted(EXPLICIT_RULE_BLOCK_SHA256)
            },
            "schema_specializations": [
                {
                    "item": item_id,
                    "schema_family": specialization.schema_family,
                    "normative_rules": list(specialization.normative_rules),
                }
                for item_id, specialization in sorted(SCHEMA_SPECIALIZATIONS.items())
            ],
            "supplemental_rule_item_attributions": [
                {"rule": rule_id, "items": list(item_ids)}
                for rule_id, item_ids in sorted(SUPPLEMENTAL_RULE_ITEM_ATTRIBUTIONS.items())
            ],
            "unqualified_signature_exemptions": [
                {"rule": rule_id, "signature": signature}
                for rule_id, signature in sorted(UNQUALIFIED_SIGNATURE_EXEMPTIONS)
            ],
            "record_identity": "record_id = id + '#' + facet",
            "facets": sorted(ITEM_FACETS),
            "colliding_item_facets": {
                item_id: sorted(definitions)
                for item_id, definitions in sorted(COLLIDING_ITEM_FACETS.items())
            },
            "classification_note": (
                "The id field preserves the exact R source spelling. The facet distinguishes "
                "constant, operation and type declarations, and record_id is their unique stable "
                "inventory identity. Qualified names are conservatively recorded as public items. "
                "Explicit rule items cover unqualified schemas and cross-document imports. An "
                "exact literal specialization has its own record related to the closed family and "
                "never implies that other family members are implemented. The item_kind and "
                "implementation record must be refined before an implementation source is added. "
                "An implementation conformance_status of partial maps a real component but does "
                "not complete the public item and blocks the release coverage gate."
            ),
        },
        "modules": module_records(),
        "source_aliases": source_aliases,
        "method_catalogue": {
            "path": "library/standard_methods.json",
            "sha256": hashlib.sha256((root / "library/standard_methods.json").read_bytes()).hexdigest(),
        },
        "items": items,
    }


def load_existing(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("inventory root must be an object")
    return value


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--specification", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--verify", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        existing = load_existing(arguments.inventory)
        expected = build_inventory(arguments.specification, existing)
        expected_text = canonical_json(expected)
        if arguments.write:
            arguments.inventory.parent.mkdir(parents=True, exist_ok=True)
            arguments.inventory.write_text(expected_text, encoding="utf-8", newline="\n")
            return 0
        if existing is None:
            print(f"missing inventory: {arguments.inventory}", file=sys.stderr)
            return 1
        actual_text = arguments.inventory.read_text(encoding="utf-8")
        if actual_text != expected_text:
            print(
                "library implementation inventory is stale; regenerate it with --write",
                file=sys.stderr,
            )
            return 1
        return 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"inventory generation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
