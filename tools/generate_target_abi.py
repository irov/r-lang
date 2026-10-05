#!/usr/bin/env python3
"""Generate the compiler/runtime C ABI tables from the pinned target manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


C_ABI_TYPES = (
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

EXPECTED_C_SPELLINGS = {
    "c_char": "char",
    "c_schar": "signed char",
    "c_uchar": "unsigned char",
    "c_short": "short",
    "c_ushort": "unsigned short",
    "c_int": "int",
    "c_uint": "unsigned int",
    "c_long": "long",
    "c_ulong": "unsigned long",
    "c_llong": "long long",
    "c_ullong": "unsigned long long",
    "c_bool": "_Bool",
    "c_wchar": "wchar_t",
    "c_wint": "wint_t",
    "c_int8": "int8_t",
    "c_uint8": "uint8_t",
    "c_int16": "int16_t",
    "c_uint16": "uint16_t",
    "c_int32": "int32_t",
    "c_uint32": "uint32_t",
    "c_int64": "int64_t",
    "c_uint64": "uint64_t",
    "c_intptr": "intptr_t",
    "c_uintptr": "uintptr_t",
    "c_intmax": "intmax_t",
    "c_uintmax": "uintmax_t",
    "c_float": "float",
    "c_double": "double",
    "c_long_double": "long double",
    "c_size": "size_t",
    "c_ptrdiff": "ptrdiff_t",
}

INTEGER_LIMITS = {
    "c_char": ("CHAR_MIN", "CHAR_MAX"),
    "c_schar": ("SCHAR_MIN", "SCHAR_MAX"),
    "c_uchar": (None, "UCHAR_MAX"),
    "c_short": ("SHRT_MIN", "SHRT_MAX"),
    "c_ushort": (None, "USHRT_MAX"),
    "c_int": ("INT_MIN", "INT_MAX"),
    "c_uint": (None, "UINT_MAX"),
    "c_long": ("LONG_MIN", "LONG_MAX"),
    "c_ulong": (None, "ULONG_MAX"),
    "c_llong": ("LLONG_MIN", "LLONG_MAX"),
    "c_ullong": (None, "ULLONG_MAX"),
    "c_wchar": ("WCHAR_MIN", "WCHAR_MAX"),
    "c_wint": ("WINT_MIN", "WINT_MAX"),
    "c_int8": ("INT8_MIN", "INT8_MAX"),
    "c_uint8": (None, "UINT8_MAX"),
    "c_int16": ("INT16_MIN", "INT16_MAX"),
    "c_uint16": (None, "UINT16_MAX"),
    "c_int32": ("INT32_MIN", "INT32_MAX"),
    "c_uint32": (None, "UINT32_MAX"),
    "c_int64": ("INT64_MIN", "INT64_MAX"),
    "c_uint64": (None, "UINT64_MAX"),
    "c_intptr": ("INTPTR_MIN", "INTPTR_MAX"),
    "c_uintptr": (None, "UINTPTR_MAX"),
    "c_intmax": ("INTMAX_MIN", "INTMAX_MAX"),
    "c_uintmax": (None, "UINTMAX_MAX"),
    "c_size": (None, "SIZE_MAX"),
    "c_ptrdiff": ("PTRDIFF_MIN", "PTRDIFF_MAX"),
}

FLOAT_LIMITS = {
    "c_float": "FLT",
    "c_double": "DBL",
    "c_long_double": "LDBL",
}

GENERATED_PATHS = {
    "runtime_header": Path("runtime/include/r_runtime_target_abi.h"),
    "compiler_manifest_digests": Path("compiler/codegen/target_manifest_digests.generated.inc"),
    "compiler_spelling": Path("compiler/codegen/target_c_abi_spelling.generated.inc"),
    "compiler_layout": Path("compiler/semantic/target_scalar_layout.generated.inc"),
    "compiler_integer_kind": Path("compiler/semantic/target_integer_kind.generated.inc"),
    "compiler_integer_properties": Path(
        "compiler/semantic/target_c_abi_integer_properties.generated.inc"
    ),
    "compiler_storage": Path("compiler/codegen/target_checked_storage.generated.inc"),
    "compiler_target_identity": Path("compiler/semantic/target_identity.generated.inc"),
    "convert_descriptors": Path(
        "library/std/convert/source/target_descriptors.generated.inc"
    ),
}

EXPECTED_TOOLCHAIN = {
    "c_compiler": "Apple clang",
    "c_compiler_version": "21.0.0",
    "c_compiler_build": "clang-2100.3.34.2",
    "sdk": "macOS 27.0",
    "generated_application_language": "ISO C17",
    "generated_application_extensions": False,
    "darwin_adapter_language": "Clang C17 with Blocks",
    "warnings_as_errors": True,
}

EXPECTED_STACK_ARTIFACT_PIPELINE = {
    "bootstrap_object": {
        "role": "seed-bounds-only",
        "compile_mode": "R_STACK_USAGE_MEASUREMENT=1",
        "includes_bounds_header": False,
        "linked": False,
    },
    "bounds_header": {
        "format": "R_STACK_FRAME_<function>",
        "ordering": "function-name-ascending",
        "function_selection": "canonical-generated-source-path-equality",
        "frame_kind": "static",
        "requires_nonempty_function_set": True,
        "initialization": "bootstrap-frame-size",
        "update": "per-function-max-previous-bound-and-candidate-frame",
    },
    "fixed_point": {
        "candidate_includes_bounds_header": True,
        "header_injection_flag": "-include",
        "candidate_remeasured": True,
        "function_name_set": "exactly-equal-to-bootstrap-on-every-iteration",
        "stability": "updated-bounds-exactly-equal-to-previous-bounds",
        "maximum_candidate_iterations": 16,
        "cap_exhaustion": "hard-fail-without-link",
    },
    "final_object": {
        "source": "converged-candidate-object",
        "includes_stable_bounds_header": True,
        "remeasured": True,
        "function_name_set": "exactly-equal-to-bootstrap",
        "per_function_bound": "final-less-than-or-equal-to-stable-bound",
        "bounds_header_identity": "exact-header-used-for-final-object",
        "link_input_identity": "exact-converged-candidate-object",
    },
}

EXPECTED_STACK_INSTRUMENTED_BUILDS = {
    "configurations": ["address-undefined", "thread"],
    "stack_conformance": False,
    "stack_usage_report": "diagnostic-only",
    "dynamic_or_unknown_frames": "not-validated",
    "nonconforming_marker_suffix": ".stack-usage.nonconforming",
    "link_input_identity": "instrumented-final-object",
}

EXPECTED_CHECKED_ERRORS = {
    "source_model": "unordered exact nominal throws set",
    "tag_c_type": "uint32_t",
    "success_tag": 0,
    "error_tag_order": "ascending canonical fully-qualified nominal type key",
    "payload_storage": "one union shared by the success value and all checked error payloads",
    "generated_r_to_r_abi": "explicit output-carrier parameter",
    "publication": "tag and payload become observable only after complete initialization",
    "interface_descriptor": "value type, normalized error set, tag table and target layout hash",
    "native_exception_primitives": False,
    "setjmp_longjmp": False,
}

# L39 (Core R-ERR-0005, R-IDB-005): the hosted target unwinds.
EXPECTED_PANIC = {
    "strategy": "unwind",
    "propagation": (
        "a per-thread panic state tested after calls and panic sites; no native exception "
        "unwinding and no setjmp/longjmp"
    ),
    "runtime_panics": "a panic inside runtime or library C code aborts (R-ERR-0006)",
    "unobserved_reports": (
        "delivered once to the panic hook, which writes the report line to the diagnostic sink"
    ),
    "diagnostic_sink": "file descriptor 2 through allocation-free write",
    "stack_exhaustion": "allocation-free panic followed by abort",
}

EXPECTED_FREESTANDING_PANIC = {
    "strategy": "environment-handler",
    "handler": "r_runtime_environment_panic",
    "diagnostic_sink": "environment handler; the runtime writes nothing",
    "stack_exhaustion": "environment handler with category stack_exhaustion",
}

FREESTANDING_PROFILE = "freestanding"
HOSTED_PROFILE = "hosted-native-async"
FREESTANDING_RUNTIME_HEADER = Path("runtime/freestanding/include/r_runtime_target_abi.h")
# wint_t is declared only by the hosted <wchar.h>; the freestanding header proves every other
# C ABI type and leaves c_wint to the hosted header of the same target.
FREESTANDING_UNPROVABLE_TYPES = frozenset({"c_wint"})


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def load_manifest(path: Path) -> tuple[bytes, dict[str, Any], dict[str, Any], str]:
    manifest_bytes = path.read_bytes()
    manifest = json.loads(manifest_bytes)
    require(isinstance(manifest, dict), "target manifest root must be an object")
    require(manifest.get("schema") == "r-target-manifest-0.1", "unexpected manifest schema")
    require(manifest.get("manifest_revision") == 10, "unexpected target manifest revision")
    require(
        manifest.get("status") == "draft-implementation-contract",
        "unexpected target manifest status",
    )
    require(manifest.get("conformance_claim") is False, "draft target must not claim conformance")
    identity = manifest.get("identity")
    require(isinstance(identity, dict), "target manifest identity must be an object")
    require(identity.get("target_triple") == "arm64-apple-darwin", "unexpected target triple")
    profile = identity.get("profile")
    require(profile in (HOSTED_PROFILE, FREESTANDING_PROFILE), "unexpected target profile")
    toolchain = manifest.get("toolchain")
    require(isinstance(toolchain, dict), "target manifest toolchain must be an object")
    require(toolchain == EXPECTED_TOOLCHAIN, "target manifest toolchain contract is not closed")
    core = manifest.get("core")
    require(isinstance(core, dict), "target manifest core must be an object")
    require(
        core.get("checked_errors") == EXPECTED_CHECKED_ERRORS,
        "target manifest checked-error carrier contract is not closed",
    )
    if profile == FREESTANDING_PROFILE:
        require(
            core.get("panic") == EXPECTED_FREESTANDING_PANIC,
            "freestanding panic strategy must be the environment handler and its contract closed",
        )
    else:
        require(
            core.get("panic") == EXPECTED_PANIC,
            "first target panic strategy must be unwind and its contract must be closed",
        )
    allocator = core.get("allocator")
    require(isinstance(allocator, dict), "target manifest allocator must be an object")
    if profile == FREESTANDING_PROFILE:
        require(
            allocator.get("available") is False,
            "freestanding target must not offer an allocator",
        )
    else:
        maximum_object_size = allocator.get("maximum_object_size")
        require(
            isinstance(maximum_object_size, int)
            and 0 < maximum_object_size <= ((1 << 64) - 1),
            "target manifest maximum object size must be a positive 64-bit integer",
        )
    stack = core.get("stack")
    require(isinstance(stack, dict), "target manifest stack contract must be an object")
    for field in (
        "protected_low_bytes",
        "call_transition_bytes",
        "generated_frame_ceiling_bytes",
    ):
        require(
            isinstance(stack.get(field), int) and stack[field] > 0,
            f"target manifest stack {field} must be a positive integer",
        )
    frame_measurement = stack.get("frame_measurement")
    require(
        isinstance(frame_measurement, dict),
        "target manifest stack frame_measurement must be an object",
    )
    require(
        frame_measurement.get("artifact_pipeline") == EXPECTED_STACK_ARTIFACT_PIPELINE,
        "target manifest stack artifact pipeline is not closed",
    )
    require(
        frame_measurement.get("instrumented_builds") == EXPECTED_STACK_INSTRUMENTED_BUILDS,
        "target manifest instrumented stack-build contract is not closed",
    )
    preflight = stack.get("preflight")
    require(isinstance(preflight, dict), "target manifest stack preflight must be an object")
    require(
        preflight.get("policy") == "static-entry-bound",
        "target manifest stack preflight policy must be the static entry bound (R-FUNC-0004)",
    )
    require(
        preflight.get("sync_callee_gate") == "none"
        and preflight.get("type_glue_move_drop") == "none",
        "target manifest stack preflight must not gate synchronous calls or type glue",
    )
    entry_budget = stack.get("entry_budget_bytes")
    require(
        isinstance(entry_budget, int) and 0 < entry_budget <= stack["generated_frame_ceiling_bytes"] * 16,
        "target manifest entry_budget_bytes must be a positive integer within the frame ceiling scale",
    )
    abi = core.get("c_abi_numeric_types")
    require(isinstance(abi, dict), "target manifest C ABI table must be an object")
    require(abi.get("schema") == "r-c-abi-numeric-types-0.1", "unexpected C ABI schema")
    require(abi.get("byte_width_bits") == 8, "R 0.1 requires eight-bit bytes")
    derived_from = abi.get("derived_from")
    require(isinstance(derived_from, dict), "target manifest C ABI provenance must be an object")
    require(
        derived_from.get("compiler")
        == f"{toolchain['c_compiler']} {toolchain['c_compiler_version']}",
        "target manifest C ABI compiler provenance does not match the toolchain",
    )
    require(
        derived_from.get("compiler_build") == toolchain["c_compiler_build"],
        "target manifest C ABI compiler-build provenance does not match the toolchain",
    )
    require(
        derived_from.get("sdk") == toolchain["sdk"],
        "target manifest C ABI SDK provenance does not match the toolchain",
    )
    require(
        derived_from.get("language") == toolchain["generated_application_language"],
        "target manifest C ABI language provenance does not match the toolchain",
    )
    types = abi.get("types")
    require(isinstance(types, dict), "target manifest C ABI types must be an object")
    require(set(types) == set(C_ABI_TYPES), "target manifest C ABI type set is not closed")
    for name in C_ABI_TYPES:
        record = types.get(name)
        require(isinstance(record, dict), f"{name} descriptor must be an object")
        require(record.get("available") is True, f"{name} must be available on this target")
        require(
            record.get("c_spelling") == EXPECTED_C_SPELLINGS[name],
            f"{name} has an invalid C spelling",
        )
        require(isinstance(record.get("object_size_bytes"), int), f"{name} size is invalid")
        require(isinstance(record.get("alignment_bytes"), int), f"{name} alignment is invalid")
    return manifest_bytes, core, types, profile


def signed_constant(value: int) -> str:
    if value == -(1 << 63):
        return "(-INT64_C(9223372036854775807) - INT64_C(1))"
    return f"INT64_C({value})"


def unsigned_constant(value: int) -> str:
    return f"UINT64_C({value})"


def render_digest_initializer(digest: bytes) -> list[str]:
    lines = ["#define R_RUNTIME_TARGET_MANIFEST_SHA256_BYTES \\", "    { \\"]
    for offset in range(0, len(digest), 8):
        values = ", ".join(f"UINT8_C(0x{value:02x})" for value in digest[offset : offset + 8])
        suffix = ", \\" if offset + 8 < len(digest) else " \\"
        lines.append(f"        {values}{suffix}")
    lines.append("    }")
    return lines


def render_integer_assertions(name: str, record: dict[str, Any]) -> list[str]:
    spelling = record["c_spelling"]
    size = record["object_size_bytes"]
    alignment = record["alignment_bytes"]
    lines = [
        f"_Static_assert(sizeof({spelling}) == {size}U && _Alignof({spelling}) == {alignment}U,",
        f'               "target manifest {name} layout mismatch");',
    ]
    if name == "c_bool":
        lines.extend(
            (
                "_Static_assert((_Bool)0 == 0 && (_Bool)1 == 1 && (_Bool)2 == 1,",
                '               "target manifest c_bool representation mismatch");',
            )
        )
        return lines
    minimum_macro, maximum_macro = INTEGER_LIMITS[name]
    maximum = int(record["maximum"])
    if minimum_macro is None:
        expression = f"{maximum_macro} == {unsigned_constant(maximum)}"
    else:
        minimum = int(record["minimum"])
        expression = (
            f"{minimum_macro} == {signed_constant(minimum)} && "
            f"{maximum_macro} == {signed_constant(maximum)}"
        )
    lines.extend(
        (
            f"_Static_assert({expression},",
            f'               "target manifest {name} range mismatch");',
        )
    )
    return lines


def render_float_assertions(name: str, record: dict[str, Any]) -> list[str]:
    spelling = record["c_spelling"]
    size = record["object_size_bytes"]
    alignment = record["alignment_bytes"]
    prefix = FLOAT_LIMITS[name]
    significand = record["significand_bits"]
    minimum_exponent = int(record["minimum_normal_exponent"]) + 1
    maximum_exponent = int(record["maximum_normal_exponent"]) + 1
    return [
        f"_Static_assert(sizeof({spelling}) == {size}U && _Alignof({spelling}) == {alignment}U &&",
        f"                   FLT_RADIX == 2 && {prefix}_MANT_DIG == {significand} &&",
        f"                   {prefix}_MIN_EXP == {minimum_exponent} &&",
        f"                   {prefix}_MAX_EXP == {maximum_exponent},",
        f'               "target manifest {name} representation mismatch");',
    ]


def render_runtime_header(
    manifest_bytes: bytes, core: dict[str, Any], types: dict[str, Any], profile: str
) -> str:
    digest = hashlib.sha256(manifest_bytes).digest()
    freestanding = profile == FREESTANDING_PROFILE
    lines = [
        "/* Generated by tools/generate_target_abi.py. */",
        "#ifndef R_RUNTIME_TARGET_ABI_H",
        "#define R_RUNTIME_TARGET_ABI_H",
        "",
        "#include <float.h>",
        "#include <limits.h>",
        "#include <stddef.h>",
        "#include <stdint.h>",
    ]
    # The freestanding header uses only C17 freestanding headers; <stdint.h> carries WCHAR_MIN
    # and WCHAR_MAX and <stddef.h> carries wchar_t.
    if not freestanding:
        lines.append("#include <wchar.h>")
    lines.extend(("", f'#define R_RUNTIME_TARGET_MANIFEST_SHA256 "{digest.hex()}"'))
    lines.extend(render_digest_initializer(digest))
    stack = core["stack"]
    if not freestanding:
        maximum_object_size = core["allocator"]["maximum_object_size"]
        lines.extend(
            (
                "",
                f"_Static_assert(SIZE_MAX >= UINT64_C({maximum_object_size}),",
                '               "target manifest maximum object size is not representable");',
                "#define R_RUNTIME_TARGET_MAXIMUM_OBJECT_SIZE \\",
                f"    ((size_t)UINT64_C({maximum_object_size}))",
            )
        )
    lines.extend(
        (
            "",
            "#define R_RUNTIME_STACK_PROTECTED_LOW_BYTES \\",
            f"    ((size_t){stack['protected_low_bytes']})",
            "#define R_RUNTIME_STACK_CALL_TRANSITION_BYTES \\",
            f"    ((size_t){stack['call_transition_bytes']})",
            "#define R_RUNTIME_GENERATED_FRAME_MAX_BYTES \\",
            f"    ((size_t){stack['generated_frame_ceiling_bytes']})",
            "",
            f"_Static_assert(CHAR_BIT == {core['c_abi_numeric_types']['byte_width_bits']},",
            '               "target manifest byte width mismatch");',
            f"_Static_assert(sizeof(size_t) * CHAR_BIT == {core['usize_width']}U,",
            '               "target manifest usize width mismatch");',
            f"_Static_assert(sizeof(ptrdiff_t) * CHAR_BIT == {core['isize_width']}U,",
            '               "target manifest isize width mismatch");',
            "_Static_assert(FLT_EVAL_METHOD == 0,",
            '               "target manifest floating evaluation method mismatch");',
            "",
        )
    )
    for name in C_ABI_TYPES:
        record = types[name]
        category = record.get("category")
        if freestanding and name in FREESTANDING_UNPROVABLE_TYPES:
            lines.append(f"/* {name} ({record['c_spelling']}) is proven by the hosted header. */")
        elif category == "integer":
            lines.extend(render_integer_assertions(name, record))
        elif category == "binary_float":
            lines.extend(render_float_assertions(name, record))
        else:
            raise ValueError(f"{name} has unsupported category {category!r}")
        lines.append("")
    lines.extend(("#endif", ""))
    return "\n".join(lines)


def render_compiler_spelling(types: dict[str, Any]) -> str:
    lines = ["/* Generated by tools/generate_target_abi.py. */"]
    for name in C_ABI_TYPES:
        token = name.upper()
        lines.extend(
            (
                f"case R_TOKEN_KW_{token}:",
                f'    return "{types[name]["c_spelling"]}";',
            )
        )
    lines.append("")
    return "\n".join(lines)


def render_compiler_integer_kind(types: dict[str, Any]) -> str:
    lines = ["/* Generated by tools/generate_target_abi.py. */"]
    for name in C_ABI_TYPES:
        record = types[name]
        if record["category"] != "integer" or record["signedness"] == "boolean":
            continue
        prefix = "I" if record["signedness"] == "signed" else "U"
        width = record["width_bits"]
        if width not in (8, 16, 32, 64):
            raise ValueError(f"unsupported enum underlying integer width: {width}")
        lines.extend((f"case R_TOKEN_KW_{name.upper()}:",
                      f"    return R_SEMANTIC_TYPE_{prefix}{width};"))
    lines.append("")
    return "\n".join(lines)


def render_compiler_integer_properties(types: dict[str, Any]) -> str:
    canonical_tokens = {
        "char": "c_char",
        "signed char": "c_schar",
        "unsigned char": "c_uchar",
        "short": "c_short",
        "unsigned short": "c_ushort",
        "int": "c_int",
        "unsigned int": "c_uint",
        "long": "c_long",
        "unsigned long": "c_ulong",
        "long long": "c_llong",
        "unsigned long long": "c_ullong",
        "_Bool": "c_bool",
    }
    ranks = {"bool": 0, "char": 1, "short": 2, "int": 3, "long": 4, "long_long": 5}
    int_record = types["c_int"]
    int_minimum = int(int_record["minimum"])
    int_maximum = int(int_record["maximum"])
    lines = ["/* Generated by tools/generate_target_abi.py. */"]

    for name in C_ABI_TYPES:
        record = types[name]
        if record["category"] != "integer":
            continue
        canonical_name = canonical_tokens.get(record["canonical_c_type"])
        rank = ranks.get(record["rank"])
        if canonical_name is None or rank is None:
            raise ValueError(f"{name} has unsupported canonical integer type or rank")
        promotion_name = canonical_name
        if rank < ranks["int"]:
            minimum = int(record["minimum"])
            maximum = int(record["maximum"])
            promotion_name = (
                "c_int"
                if int_minimum <= minimum and maximum <= int_maximum
                else "c_uint"
            )
        lines.extend(
            (
                f"case R_TOKEN_KW_{name.upper()}:",
                f"    *is_signed = {'true' if record['signedness'] == 'signed' else 'false'};",
                f"    *is_boolean = {'true' if record['signedness'] == 'boolean' else 'false'};",
                f"    *width = UINT32_C({record['width_bits']});",
                f"    *rank = UINT32_C({rank});",
                f"    *canonical_token = R_TOKEN_KW_{canonical_name.upper()};",
                f"    *promoted_token = R_TOKEN_KW_{promotion_name.upper()};",
                "    return true;",
            )
        )
    lines.append("")
    return "\n".join(lines)


def storage_name(record: dict[str, Any]) -> str:
    category = record.get("category")
    if category == "binary_float":
        representation = record.get("representation")
        if representation == "iec-60559-binary32":
            return "R_C17_CHECKED_STORAGE_BINARY32"
        if representation == "iec-60559-binary64":
            return "R_C17_CHECKED_STORAGE_BINARY64"
    elif category == "integer":
        if record.get("signedness") in ("unsigned", "boolean"):
            return "R_C17_CHECKED_STORAGE_UNSIGNED_INTEGER"
        if record.get("signedness") == "signed":
            return "R_C17_CHECKED_STORAGE_SIGNED_INTEGER"
    raise ValueError("unsupported checked-conversion storage descriptor")


def render_compiler_layout(types: dict[str, Any]) -> str:
    lines = ["/* Generated by tools/generate_target_abi.py. */"]
    for name in C_ABI_TYPES:
        record = types[name]
        lines += [f"case R_TOKEN_KW_{name.upper()}:",
                  f"    *size = UINT64_C({record['object_size_bytes']});",
                  f"    *alignment = UINT64_C({record['alignment_bytes']});",
                  "    return true;"]
    return "\n".join(lines) + "\n"


def render_compiler_storage(types: dict[str, Any]) -> str:
    lines = ["/* Generated by tools/generate_target_abi.py. */"]
    for index, name in enumerate(C_ABI_TYPES, start=12):
        lines.extend(
            (
                f"case UINT32_C({index}):",
                f"    *storage = {storage_name(types[name])};",
                "    return true;",
            )
        )
    lines.append("")
    return "\n".join(lines)


def render_convert_descriptor(name: str, record: dict[str, Any]) -> list[str]:
    enum_name = f"R_STD_CONVERT_NUMERIC_TYPE_{name.upper()}"
    category = record.get("category")
    lines = [f"case {enum_name}:"]
    if category == "binary_float":
        representation = record.get("representation")
        if representation == "iec-60559-binary32":
            expression = "r_std_convert_binary32_descriptor()"
        elif representation == "iec-60559-binary64":
            expression = "r_std_convert_binary64_descriptor()"
        else:
            raise ValueError(f"{name} has unsupported floating representation")
    elif record.get("signedness") == "signed":
        maximum = int(record["maximum"])
        negative_magnitude = -int(record["minimum"])
        expression = (
            "r_std_convert_signed_descriptor("
            f"{unsigned_constant(maximum)}, {unsigned_constant(negative_magnitude)})"
        )
    elif record.get("signedness") in ("unsigned", "boolean"):
        expression = f"r_std_convert_unsigned_descriptor({unsigned_constant(int(record['maximum']))})"
    else:
        raise ValueError(f"{name} has unsupported integer signedness")
    lines.extend((f"    *descriptor = {expression};", "    return 1;"))
    return lines


def render_convert_descriptors(types: dict[str, Any]) -> str:
    lines = ["/* Generated by tools/generate_target_abi.py. */"]
    for name in C_ABI_TYPES:
        lines.extend(render_convert_descriptor(name, types[name]))
    lines.append("")
    return "\n".join(lines)


def render_manifest_digests(root: Path) -> str:
    """One table of every committed target manifest digest, keyed by its profile.

    The emitter binds a compilation to the manifest of the selected profile (R-CONF-G005):
    generated C for a manifest that is not committed here is refused.
    """
    lines = ["/* Generated by tools/generate_target_abi.py. */"]
    entries = []
    for path in sorted((root / "targets").glob("*.json")):
        if path.name.endswith(".runtime-entry-stack.json"):
            continue
        manifest = json.loads(path.read_bytes())
        profile = manifest["identity"]["profile"]
        digest = hashlib.sha256(path.read_bytes()).digest()
        entries.append((profile, digest))
    for profile, digest in sorted(entries):
        bytes_text = ", ".join(f"UINT8_C(0x{byte:02x})" for byte in digest)
        lines.append(f'{{"{profile}",')
        lines.append(f" {{{bytes_text}}}}},")
    lines.append("")
    return "\n".join(lines)


def render_target_identity(manifest_bytes: bytes) -> str:
    """The target triple that Core R-REFL-0004 exposes as core::target_name()."""
    manifest = json.loads(manifest_bytes)
    triple = manifest["identity"]["target_triple"]
    require(isinstance(triple, str) and triple.isascii(), "target triple must be ASCII text")
    return "\n".join(
        (
            "/* Generated by tools/generate_target_abi.py. */",
            f'static const char r_target_identity_triple[] = "{triple}";',
            "",
        )
    )


def generated_contents(
    root: Path,
    manifest_bytes: bytes,
    core: dict[str, Any],
    types: dict[str, Any],
    profile: str,
) -> dict[str, str]:
    return {
        "runtime_header": render_runtime_header(manifest_bytes, core, types, profile),
        "compiler_manifest_digests": render_manifest_digests(root),
        "compiler_spelling": render_compiler_spelling(types),
        "compiler_storage": render_compiler_storage(types),
        "compiler_target_identity": render_target_identity(manifest_bytes),
        "compiler_integer_kind": render_compiler_integer_kind(types),
        "compiler_integer_properties": render_compiler_integer_properties(types),
        "compiler_layout": render_compiler_layout(types),
        "convert_descriptors": render_convert_descriptors(types),
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--verify", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    try:
        manifest_bytes, core, types, profile = load_manifest(arguments.manifest.resolve())
        contents = generated_contents(root, manifest_bytes, core, types, profile)
        paths = dict(GENERATED_PATHS)
        if profile == FREESTANDING_PROFILE:
            # The compiler-side tables are shared with the hosted target and must agree with
            # it; only the runtime header differs between the two manifests.
            paths["runtime_header"] = FREESTANDING_RUNTIME_HEADER
        stale: list[Path] = []
        for key, relative in paths.items():
            path = root / relative
            expected = contents[key]
            if arguments.write:
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.exists() or path.read_text(encoding="utf-8") != expected:
                    path.write_text(expected, encoding="utf-8", newline="\n")
            elif not path.exists() or path.read_text(encoding="utf-8") != expected:
                stale.append(relative)
        if stale:
            for path in stale:
                print(f"target ABI generated file is stale: {path}", file=sys.stderr)
            return 1
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"target ABI generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
