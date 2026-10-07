#!/usr/bin/env python3
"""Regression tests for normative rule catalogs and the first target contract."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RULE_GENERATOR = REPOSITORY_ROOT / "tools/generate_rule_inventory.py"
TARGET_CHECKER = REPOSITORY_ROOT / "tools/check_target_manifest.py"
TARGET_ABI_GENERATOR = REPOSITORY_ROOT / "tools/generate_target_abi.py"
CORE_SPECIFICATION = REPOSITORY_ROOT / "specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc"
LIBRARY_SPECIFICATION = (
    REPOSITORY_ROOT / "specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc"
)
CORE_INVENTORY = (
    REPOSITORY_ROOT / "specification/generated/R_LANGUAGE_SPECIFICATION_0_1.rules.json"
)
LIBRARY_INVENTORY = (
    REPOSITORY_ROOT
    / "specification/generated/R_STANDARD_LIBRARY_SPECIFICATION_0_1.rules.json"
)
TARGET_MANIFEST = (
    REPOSITORY_ROOT / "targets/arm64-apple-darwin.hosted-native-async.json"
)
FREESTANDING_MANIFEST = REPOSITORY_ROOT / "targets/arm64-apple-darwin.freestanding.json"


def run_tool(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


class SpecificationContractTests(unittest.TestCase):
    def run_target_with_manifest(self, value: dict[str, Any]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            manifest = Path(temporary_directory) / "target.json"
            write_json(manifest, value)
            return run_tool(str(TARGET_CHECKER), "--manifest", str(manifest))

    def run_target_abi_with_manifest(
        self, value: dict[str, Any]
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            manifest = Path(temporary_directory) / "target.json"
            write_json(manifest, value)
            return run_tool(
                str(TARGET_ABI_GENERATOR),
                "--root",
                str(REPOSITORY_ROOT),
                "--manifest",
                str(manifest),
                "--verify",
            )

    def test_repository_catalogs_and_target_manifest_are_current(self) -> None:
        for specification, inventory, expected_count in (
            (CORE_SPECIFICATION, CORE_INVENTORY, "501 rules"),
            (LIBRARY_SPECIFICATION, LIBRARY_INVENTORY, "478 rules"),
        ):
            result = run_tool(
                str(RULE_GENERATOR),
                "--specification",
                str(specification),
                "--inventory",
                str(inventory),
                "--verify",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(expected_count, result.stdout)

        target = run_tool(str(TARGET_CHECKER))
        self.assertEqual(target.returncode, 0, target.stderr)
        self.assertIn("4 filesystem-lane threads", target.stdout)
        self.assertIn("4 blocking-pool threads", target.stdout)
        self.assertIn("262144-byte generated-frame ceiling", target.stdout)

    def test_freestanding_target_manifest_is_current(self) -> None:
        target = run_tool(str(TARGET_CHECKER), "--manifest", str(FREESTANDING_MANIFEST))
        self.assertEqual(target.returncode, 0, target.stderr)
        self.assertIn("arm64-apple-darwin freestanding", target.stdout)
        self.assertIn("environment panic handler", target.stdout)
        header = run_tool(
            str(TARGET_ABI_GENERATOR),
            "--root",
            str(REPOSITORY_ROOT),
            "--manifest",
            str(FREESTANDING_MANIFEST),
            "--verify",
        )
        self.assertEqual(header.returncode, 0, header.stderr)

    def test_freestanding_manifest_shares_the_c_abi_with_the_hosted_target(self) -> None:
        value = json.loads(FREESTANDING_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["usize_width"] = 32
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("core.usize_width must equal the hosted target record", result.stderr)

    def test_freestanding_manifest_rejects_an_allocator(self) -> None:
        value = json.loads(FREESTANDING_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["allocator"]["available"] = True
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("freestanding allocator contract is not closed", result.stderr)

    def test_freestanding_manifest_rejects_hosted_records(self) -> None:
        value = json.loads(FREESTANDING_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"] = {}
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("freestanding manifest must not carry hosted records", result.stderr)

    def test_freestanding_manifest_requires_the_environment_panic_handler(self) -> None:
        value = json.loads(FREESTANDING_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["panic"]["strategy"] = "abort"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("freestanding panic contract is not closed", result.stderr)

    def test_freestanding_target_abi_header_omits_hosted_headers(self) -> None:
        header = (
            REPOSITORY_ROOT / "runtime/freestanding/include/r_runtime_target_abi.h"
        ).read_text(encoding="utf-8")
        self.assertNotIn("<wchar.h>", header)
        self.assertNotIn("R_RUNTIME_TARGET_MAXIMUM_OBJECT_SIZE", header)
        self.assertIn("R_RUNTIME_STACK_PROTECTED_LOW_BYTES", header)

    def test_catalog_records_normative_source_without_coverage_claim(self) -> None:
        for inventory_path, rule_id in (
            (CORE_INVENTORY, "R-CMAP-0039"),
            (LIBRARY_INVENTORY, "R-SLIB-MAP-B009"),
        ):
            inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
            self.assertEqual(inventory["catalog_kind"], "normative_rule_catalog")
            self.assertIsNone(inventory["implementation_coverage"])
            record = next(rule for rule in inventory["rules"] if rule["id"] == rule_id)
            self.assertEqual(record["anchor"], rule_id)
            self.assertTrue(record["headings"])
            self.assertGreater(record["source_location"]["declaration_line"], 0)
            self.assertRegex(record["text_sha256"], r"^[0-9a-f]{64}$")

    def test_stale_rule_inventory_is_rejected(self) -> None:
        value = json.loads(CORE_INVENTORY.read_text(encoding="utf-8"))
        value["rules"][0]["text_sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as temporary_directory:
            inventory = Path(temporary_directory) / "rules.json"
            write_json(inventory, value)
            result = run_tool(
                str(RULE_GENERATOR),
                "--specification",
                str(CORE_SPECIFICATION),
                "--inventory",
                str(inventory),
                "--verify",
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("stale", result.stderr)

    def test_target_revision_mismatch_is_rejected(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["identity"]["core_specification_revision"] = "0.1.0-draft.20"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("target/Core inventory revision mismatch", result.stderr)

    def test_target_manifest_revision_is_current(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["manifest_revision"] = 5
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("target manifest revision must be 10", result.stderr)

    def test_checked_error_carrier_contract_is_closed(self) -> None:
        mutations = (
            ("tag_c_type", "uint16_t"),
            ("success_tag", 1),
            ("error_tag_order", "declaration order"),
            ("generated_r_to_r_abi", "C struct return"),
            ("native_exception_primitives", True),
            ("setjmp_longjmp", True),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["core"]["checked_errors"][field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("checked-error carrier contract is not closed", result.stderr)

                result = self.run_target_abi_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("checked-error carrier contract is not closed", result.stderr)

    def test_first_target_rejects_abort_panic_strategy(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["panic"]["strategy"] = "abort"

        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("first target panic strategy must be unwind", result.stderr)

        result = self.run_target_abi_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("first target panic strategy must be unwind", result.stderr)

    def test_target_abi_panic_contract_is_closed(self) -> None:
        mutations = (
            ("diagnostic_sink", "stdio stderr"),
            ("stack_exhaustion", "implementation-defined"),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["core"]["panic"][field] = replacement
                result = self.run_target_abi_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("panic strategy", result.stderr)
                self.assertIn("contract must be closed", result.stderr)

    def test_sha_message_length_object_bound_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["allocator"]["maximum_object_size"] = 9223372036854775807
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("SHA-1/SHA-256 message-length domain", result.stderr)

    def test_darwin_toolchain_contract_is_closed(self) -> None:
        mutations = (
            ("c_compiler", "host cc"),
            ("c_compiler_version", "21.0.1"),
            ("c_compiler_build", "clang-2100.1.1.102"),
            ("sdk", "macOS 26.6"),
            ("warnings_as_errors", False),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["toolchain"][field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("Darwin toolchain contract is not closed", result.stderr)

        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["toolchain"]["host_probe"] = True
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Darwin toolchain contract is not closed", result.stderr)

    def test_darwin_stack_contract_is_closed(self) -> None:
        mutations = (
            (("growth_direction",), "up"),
            (("bounds", "address_api"), "pthread_get_stackbase_np"),
            (("bounds", "storage"), "process global"),
            (("protected_low_bytes",), 32768),
            (("call_transition_bytes",), 8192),
            (("generated_frame_ceiling_bytes",), 524288),
            (("frame_measurement", "compiler"), "host cc"),
            (
                ("frame_measurement", "compile_flags"),
                ["-std=c17", "-O0", "-fstack-usage"],
            ),
            (("frame_measurement", "link_time_optimization"), True),
            (("frame_measurement", "generated_function_inlining"), True),
            (
                ("frame_measurement", "artifact_pipeline", "bootstrap_object", "linked"),
                True,
            ),
            (
                (
                    "frame_measurement",
                    "artifact_pipeline",
                    "bounds_header",
                    "function_selection",
                ),
                "basename-only",
            ),
            (
                (
                    "frame_measurement",
                    "artifact_pipeline",
                    "bounds_header",
                    "update",
                ),
                "candidate-frame-only",
            ),
            (
                (
                    "frame_measurement",
                    "artifact_pipeline",
                    "fixed_point",
                    "function_name_set",
                ),
                "subset",
            ),
            (
                (
                    "frame_measurement",
                    "artifact_pipeline",
                    "fixed_point",
                    "stability",
                ),
                "candidate-equals-previous-candidate",
            ),
            (
                (
                    "frame_measurement",
                    "artifact_pipeline",
                    "fixed_point",
                    "maximum_candidate_iterations",
                ),
                0,
            ),
            (
                (
                    "frame_measurement",
                    "artifact_pipeline",
                    "final_object",
                    "per_function_bound",
                ),
                "unchecked",
            ),
            (
                (
                    "frame_measurement",
                    "artifact_pipeline",
                    "final_object",
                    "bounds_header_identity",
                ),
                "regenerated-after-final-compile",
            ),
            (
                ("frame_measurement", "instrumented_builds", "stack_conformance"),
                True,
            ),
            (
                (
                    "frame_measurement",
                    "instrumented_builds",
                    "nonconforming_marker_suffix",
                ),
                "",
            ),
            (("preflight", "policy"), "per-call-gates"),
            (("preflight", "sync_callee_gate"), "inside each callee"),
            (("preflight", "type_glue_move_drop"), "inside each glue"),
            (("entry_budget_bytes",), 262144),
            (("thread_initialization", "attached_thread"), "after attachment commit"),
        )
        for path, replacement in mutations:
            with self.subTest(path=path):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                record = value["core"]["stack"]
                for field in path[:-1]:
                    record = record[field]
                record[path[-1]] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("Darwin stack contract is not closed", result.stderr)

        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["stack"]["implementation_note"] = "open extension"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Darwin stack contract is not closed", result.stderr)

    def test_target_abi_generator_rejects_open_stack_and_toolchain_contracts(self) -> None:
        mutations = (
            (
                ("toolchain", "c_compiler_build"),
                "clang-2100.1.1.102",
                "toolchain contract is not closed",
            ),
            (
                (
                    "core",
                    "stack",
                    "frame_measurement",
                    "artifact_pipeline",
                    "bounds_header",
                    "update",
                ),
                "replace-with-candidate-frame",
                "stack artifact pipeline is not closed",
            ),
            (
                (
                    "core",
                    "stack",
                    "frame_measurement",
                    "artifact_pipeline",
                    "fixed_point",
                    "maximum_candidate_iterations",
                ),
                17,
                "stack artifact pipeline is not closed",
            ),
            (
                (
                    "core",
                    "stack",
                    "frame_measurement",
                    "artifact_pipeline",
                    "fixed_point",
                    "stability",
                ),
                "candidate-measurement-unchanged",
                "stack artifact pipeline is not closed",
            ),
            (
                (
                    "core",
                    "stack",
                    "frame_measurement",
                    "artifact_pipeline",
                    "final_object",
                    "link_input_identity",
                ),
                "recompiled-after-convergence",
                "stack artifact pipeline is not closed",
            ),
            (
                (
                    "core",
                    "stack",
                    "frame_measurement",
                    "artifact_pipeline",
                    "final_object",
                    "bounds_header_identity",
                ),
                "equivalent-regenerated-header",
                "stack artifact pipeline is not closed",
            ),
            (
                (
                    "core",
                    "stack",
                    "frame_measurement",
                    "instrumented_builds",
                    "stack_conformance",
                ),
                True,
                "instrumented stack-build contract is not closed",
            ),
            (
                ("core", "stack", "preflight", "type_glue_move_drop"),
                "inside each glue",
                "stack preflight must not gate synchronous calls or type glue",
            ),
            (
                ("core", "stack", "entry_budget_bytes"),
                0,
                "entry_budget_bytes must be a positive integer",
            ),
        )
        for path, replacement, diagnostic in mutations:
            with self.subTest(path=path):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                record: Any = value
                for field in path[:-1]:
                    record = record[field]
                record[path[-1]] = replacement
                result = self.run_target_abi_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(diagnostic, result.stderr)

    def test_c_abi_numeric_type_set_is_closed(self) -> None:
        for mutation in ("remove", "add"):
            with self.subTest(mutation=mutation):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                types = value["core"]["c_abi_numeric_types"]["types"]
                if mutation == "remove":
                    del types["c_wint"]
                else:
                    types["c_int128"] = dict(types["c_int64"])
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("C ABI numeric type set is not closed", result.stderr)

    def test_c_abi_numeric_integer_records_are_exact(self) -> None:
        mutations = (
            ("c_char", "signedness", "unsigned"),
            ("c_short", "alignment_bytes", 4),
            ("c_int", "object_size_bytes", 8),
            ("c_ulong", "maximum", "9223372036854775807"),
            ("c_int64", "rank", "long"),
            ("c_int8", "available", False),
            ("c_int8", "canonical_c_type", "char"),
            ("c_wchar", "minimum", "0"),
            ("c_wint", "available", False),
            ("c_bool", "width_bits", 1),
        )
        for name, field, replacement in mutations:
            with self.subTest(name=name, field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["core"]["c_abi_numeric_types"]["types"][name][field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    f"C ABI numeric type {name} field {field} does not match the pinned target",
                    result.stderr,
                )

    def test_c_abi_numeric_integer_representation_proofs_are_exact(self) -> None:
        mutations = (
            ("two's-complement", "padding_bits", 1),
            ("two's-complement", "has_trap_representation", True),
            ("pure-binary", "padding_bits", 1),
            ("c17-_Bool", "value_bits", 8),
            ("c17-_Bool", "padding_bits", 0),
            ("c17-_Bool", "value_encoding", "nonzero=true"),
        )
        for representation, field, replacement in mutations:
            with self.subTest(representation=representation, field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                contracts = value["core"]["c_abi_numeric_types"][
                    "integer_representation_contracts"
                ]
                contracts[representation][field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "field integer_representation_contracts does not match the pinned target",
                    result.stderr,
                )

    def test_c_abi_binary_float_records_are_exact(self) -> None:
        mutations = (
            ("c_float", "alignment_bytes", 8),
            ("c_float", "significand_bits", 23),
            ("c_double", "minimum_subnormal_exponent", -1073),
            ("c_double", "maximum_finite", "0x1p+1023"),
            ("c_long_double", "representation", "iec-60559-binary128"),
            ("c_long_double", "object_size_bytes", 16),
        )
        for name, field, replacement in mutations:
            with self.subTest(name=name, field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["core"]["c_abi_numeric_types"]["types"][name][field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    f"C ABI numeric type {name} field {field} does not match the pinned target",
                    result.stderr,
                )

    def test_c_abi_binary_float_operation_contract_is_exact(self) -> None:
        mutations = (
            ("evaluation_method", 1),
            ("rounding", "toward_zero"),
            ("gradual_underflow", False),
            ("iec_60559_operation_mapping", ["add", "subtract"]),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["core"]["c_abi_numeric_types"]["binary_float_contract"][field] = (
                    replacement
                )
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "field binary_float_contract does not match the pinned target",
                    result.stderr,
                )

    def test_c_abi_numeric_table_provenance_is_pinned(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["c_abi_numeric_types"]["derived_from"]["target"] = (
            "host-default"
        )
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "field derived_from does not match the pinned target",
            result.stderr,
        )

    def test_c_abi_numeric_record_field_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["c_abi_numeric_types"]["types"]["c_int"]["host_probe"] = True
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("C ABI numeric type c_int field set is not closed", result.stderr)

    def test_c_abi_numeric_table_field_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["core"]["c_abi_numeric_types"]["host_defaults"] = True
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("C ABI numeric type table field set is not closed", result.stderr)

    def test_filesystem_lane_requires_exact_thread_count(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"]["thread_count"] = 5
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must have 4 threads", result.stderr)

    def test_timer_contract_distinguishes_continuous_rearming(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["timers"]["clock_bridge"] = (
            "continuous remaining duration is converted once"
        )
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must distinguish std.fs continuous re-arming", result.stderr)

    def test_filesystem_lane_native_entry_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"][
            "allowed_native_entries"
        ].append(
            {
                "name": "read",
                "flags": [],
                "family": "payload_read",
                "commit": "none",
            }
        )
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("native-entry set is not closed", result.stderr)

    def test_filesystem_lane_implemented_entry_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"]["implementation"][
            "supported_native_entries"
        ].remove("fstat")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("implemented native-entry set", result.stderr)

    def test_filesystem_lane_openat_flag_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        entries = value["hosted_native_async"]["filesystem_adapter_lane"][
            "allowed_native_entries"
        ]
        openat = next(entry for entry in entries if entry["name"] == "openat")
        openat["flags"].remove("O_RESOLVE_BENEATH")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("openat flag set is not closed", result.stderr)

    def test_filesystem_lane_namespace_flag_sets_are_closed(self) -> None:
        for entry_name, flag in (
            ("fstatat", "AT_RESOLVE_BENEATH"),
            ("unlinkat", "AT_SYMLINK_NOFOLLOW_ANY"),
            ("renameatx_np", "RENAME_NOFOLLOW_ANY"),
        ):
            value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
            entries = value["hosted_native_async"]["filesystem_adapter_lane"][
                "allowed_native_entries"
            ]
            entry = next(item for item in entries if item["name"] == entry_name)
            entry["flags"].remove(flag)
            result = self.run_target_with_manifest(value)
            self.assertEqual(result.returncode, 1)
            self.assertIn(f"{entry_name} flag set is not closed", result.stderr)

    def test_filesystem_lane_open_or_create_retry_is_bounded(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"]["implementation"][
            "open_or_create_retry_limit"
        ] = 17
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("retry limit must be exactly 16", result.stderr)

    def test_filesystem_lane_directory_staging_contract_is_closed(self) -> None:
        mutations = (
            ("directory_staging_prefix", ".unsafe-stage-", "staging prefix must be exactly"),
            ("directory_staging_mode", "0777", "staging mode must be exactly"),
            (
                "directory_staging_uses",
                ["std.fs::create_directory_beneath"],
                "staging use set is not closed",
            ),
            (
                "directory_staging_payload_name",
                "content",
                "file staging payload name must be exactly payload",
            ),
            (
                "directory_staging_payload_mode",
                "0666",
                "file staging payload mode must be exactly 0600",
            ),
            (
                "directory_staging_collision_retry_limit",
                17,
                "staging collision retry limit must be exactly 16",
            ),
            (
                "directory_staging_collision_exhaustion",
                "retry forever",
                "staging retry exhaustion",
            ),
            ("directory_staging_cleanup", "best effort", "exact directory staging cleanup"),
            (
                "directory_staging_cleanup_failure",
                "ignore failure",
                "directory staging cleanup failure handling",
            ),
            (
                "atomic_write_beneath_publication",
                "rename relative to a derived parent descriptor",
                "root-relative beneath atomic-write publication",
            ),
            (
                "atomic_write_postcommit",
                "late durability failure selects failed",
                "atomic-write post-commit outcome stability",
            ),
        )

        for field, replacement, diagnostic in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                implementation = value["hosted_native_async"]["filesystem_adapter_lane"][
                    "implementation"
                ]
                implementation[field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(diagnostic, result.stderr)

    def test_filesystem_lane_final_symlink_entry_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"]["implementation"][
            "final_symlink_entry_operations"
        ].remove("std.fs::metadata_beneath")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("final-symlink entry operation set is not closed", result.stderr)

    def test_filesystem_reserved_staging_component_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["standard_library"]["reserved_beneath_components"] = []
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("reserved beneath component set is not closed", result.stderr)

    def test_atomic_whole_file_no_replace_contract_is_closed(self) -> None:
        original = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        fields = tuple(original["standard_library"]["atomic_no_replace"])
        for field in fields:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["standard_library"]["atomic_no_replace"][field] = "not conforming"
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("atomic whole-file no-replace contract is not closed", result.stderr)

    def test_filesystem_lane_maximum_path_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"]["implementation"][
            "maximum_path_bytes"
        ] = 1048576
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("maximum path must be exactly 1048575 bytes", result.stderr)

    def test_partial_filesystem_lane_public_integration_scope_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"]["implementation"][
            "public_std_fs_integrated_operations"
        ].append("std.fs::not_an_operation")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("integrated std.fs operation set is not closed", result.stderr)

    def test_partial_filesystem_lane_cannot_claim_profile_conformance(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["filesystem_adapter_lane"]["implementation"][
            "profile_conformance_claim"
        ] = True
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must not claim profile conformance", result.stderr)

    def test_payload_adapter_operation_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["dispatch_io_payload_adapter"]["implementation"][
            "supported_operations"
        ].append("fsync")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("payload adapter operation set is not closed", result.stderr)

    def test_payload_adapter_blocks_unit_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["dispatch_io_payload_adapter"]["implementation"][
            "blocks_enabled_units"
        ].remove("source/io_read.c")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("payload adapter Blocks-unit set is not closed", result.stderr)

    def test_partial_payload_adapter_public_integration_scope_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["dispatch_io_payload_adapter"]["implementation"][
            "public_std_io_integrated_operations"
        ].append("std.io::not_an_operation")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("integrated std.io operation set is not closed", result.stderr)

    def test_console_payload_requires_final_handler_acknowledgement(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        matrix = value["hosted_native_async"]["capability_matrix"]
        record = next(
            item for item in matrix if item["family"] == "console_and_process_pipe_payload"
        )
        record["acknowledgement"] = "Dispatch I/O cleanup completion"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("final Dispatch I/O handler", result.stderr)

    def test_file_payload_requires_adapter_mode(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        matrix = value["hosted_native_async"]["capability_matrix"]
        record = next(item for item in matrix if item["family"] == "file_payload_read_write")
        record["cancellation"] = "native"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("queued_or_acknowledged_after_return", result.stderr)

    def test_tcp_payload_requires_terminal_transition_acknowledgement(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        matrix = value["hosted_native_async"]["capability_matrix"]
        record = next(item for item in matrix if item["family"] == "tcp_payload")
        record["acknowledgement"] = "final Dispatch I/O handler"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "tcp_payload must acknowledge at the request terminal transition", result.stderr
        )

    def test_file_payload_adapter_record_is_closed(self) -> None:
        mutations = (
            ("maximum_entered_transfers", 8, "maximum_entered_transfers must be 4"),
            ("may_execute_r_code", True, "may_execute_r_code must be False"),
            ("native_entries", ["pread", "pwrite", "write", "fsync"], "native_entries must be"),
        )
        for field, replacement, message in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["hosted_native_async"]["file_payload_adapter"][field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(message, result.stderr)

    def test_payload_engine_assignment_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["dispatch_io_payload_adapter"]["implementation"]["engines"][
            "dispatch_io"
        ].append("stream_file")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("payload adapter engine assignment is not closed", result.stderr)

    def test_dns_resolver_adapter_contract_is_closed(self) -> None:
        mutations = (
            ("supported_operations", []),
            ("task_local_validation", "defer validation to DNS-SD"),
            ("numeric_resolution", "submit every host to DNS-SD"),
            ("result_ordering", "sort resolver results"),
            (
                "cancellation_deadline_acknowledgement",
                "release task storage before callback quiescence",
            ),
            ("profile_conformance_claim", False),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["hosted_native_async"]["dns_resolver_adapter"]["implementation"][field] = (
                    replacement
                )
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("DNS resolver", result.stderr)

    def test_network_adapter_operation_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["nonblocking_socket_adapter"]["implementation"][
            "supported_operations"
        ].remove("std.net::tcp_write_all")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("network adapter operation set is not closed", result.stderr)

    def test_tcp_write_adapter_contract_is_closed(self) -> None:
        mutations = (
            ("tcp_write_submission", "reservation after task commit"),
            ("tcp_write_completion", "zero progress is successful"),
            ("tcp_write_ordering", "empty buffers wait in the write FIFO"),
            (
                "tcp_write_cancellation_deadline_acknowledgement",
                "buffer released before native acknowledgement",
            ),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["hosted_native_async"]["nonblocking_socket_adapter"]["implementation"][
                    field
                ] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("TCP write", result.stderr)

    def test_tcp_shutdown_and_close_adapter_contracts_are_closed(self) -> None:
        mutations = (
            ("tcp_shutdown_submission", "native shutdown before task commit"),
            ("tcp_shutdown_ordering", "both waits only for writes"),
            (
                "tcp_shutdown_commit_cancellation_deadline_acknowledgement",
                "late cancellation replaces native shutdown",
            ),
            ("tcp_close_submission", "stream consumed before reservation"),
            ("tcp_close_terminal_cleanup", "best-effort descriptor release"),
            (
                "tcp_close_cancellation_deadline_acknowledgement",
                "task acknowledged before native cleanup",
            ),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["hosted_native_async"]["nonblocking_socket_adapter"]["implementation"][
                    field
                ] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("TCP", result.stderr)

    def test_udp_data_and_close_adapter_contracts_are_closed(self) -> None:
        mutations = (
            ("udp_datagram_submission_and_ownership", "reservation after task commit"),
            ("udp_receive_buffer_contract", "native receive writes into the caller array"),
            ("udp_direction_ordering", "sends and receives share one FIFO"),
            (
                "udp_send_commit_cancellation_deadline_acknowledgement",
                "late cancellation replaces native acceptance",
            ),
            ("udp_close_terminal_cleanup", "best-effort descriptor release"),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["hosted_native_async"]["nonblocking_socket_adapter"]["implementation"][
                    field
                ] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("UDP", result.stderr)

    def test_process_spawn_adapter_contract_is_closed(self) -> None:
        mutations = (
            ("exact_program_invocation", "spawnp through PATH"),
            ("start_failure_operand_preservation", "command consumed before reservation"),
            ("creation_commit", "child resumed before observer installation"),
            ("cancellation_deadline", "late cancellation replaces creation commit"),
            ("pipe_reservation", "pipe roots created after child publication"),
            ("pipe_extraction", "two racing calls receive the same endpoint"),
            ("wait_reservation", "observation slot allocated after task commit"),
            ("wait_completion", "blocking waitpid on executor worker"),
            ("terminate_commit", "late cancellation replaces accepted SIGKILL"),
            ("child_status_mapping", "platform status narrowed implicitly"),
            ("rollback", "best-effort asynchronous cleanup"),
            ("child_lifecycle", "child view owns the only reap observer"),
            ("hosted_shutdown", "does not wait for pipe cleanup"),
        )
        for field, replacement in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                value["hosted_native_async"]["process_spawn_adapter"]["implementation"][
                    field
                ] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn("process spawn adapter", result.stderr)

    def test_process_spawn_unimplemented_operation_set_is_closed(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["process_spawn_adapter"]["implementation"][
            "public_unimplemented_operations"
        ].append("std.process::not_an_operation")
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unimplemented operation set is not closed", result.stderr)

    def test_payload_adapter_requires_submission_ordering(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["dispatch_io_payload_adapter"]["implementation"][
            "same_direction_submission_order"
        ] = "not_implemented"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must record implemented per-direction submission ordering", result.stderr)

    def test_std_fs_payload_requires_one_file_adapter_handle(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        implementation = value["hosted_native_async"]["dispatch_io_payload_adapter"][
            "implementation"
        ]
        implementation["std_fs_payload_handle"] = "one Dispatch I/O channel per operation"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must use one file payload adapter handle", result.stderr)

    def test_std_fs_nonappend_positioning_requires_positioned_calls(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        implementation = value["hosted_native_async"]["dispatch_io_payload_adapter"][
            "implementation"
        ]
        implementation["std_fs_nonappend_positioning"] = (
            "external lseek precedes each RANDOM child request"
        )
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must pass their position to pread or pwrite", result.stderr)

    def test_std_fs_append_positioning_requires_stream_root_and_o_append(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        implementation = value["hosted_native_async"]["dispatch_io_payload_adapter"][
            "implementation"
        ]
        implementation["std_fs_append_positioning"] = "seek to end before each write"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must record O_APPEND and logical-position semantics", result.stderr)

    def test_std_fs_deadline_recomputes_continuous_clock_remainder(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        implementation = value["hosted_native_async"]["dispatch_io_payload_adapter"][
            "implementation"
        ]
        implementation["std_fs_deadline_clock"] = (
            "absolute deadline converted once to a relative timer"
        )
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must recompute the continuous-clock remainder and re-arm", result.stderr)

    def test_std_fs_explicit_close_requires_handle_root_acknowledgement(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        implementation = value["hosted_native_async"]["dispatch_io_payload_adapter"][
            "implementation"
        ]
        implementation["std_fs_explicit_close"] = "release root view and return immediately"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must wait for handle root cleanup", result.stderr)

    def test_std_fs_whole_file_read_contract_is_closed(self) -> None:
        mutations = (
            (
                "std_fs_whole_file_read_operations",
                ["std.fs::read_file"],
                "whole-file read operation set is not closed",
            ),
            (
                "std_fs_whole_file_read_pipeline",
                "blocking read loop",
                "must record the exact native pipeline",
            ),
            (
                "std_fs_whole_file_read_path_policy",
                "follow every symbolic link",
                "must record ordinary and beneath symlink policy",
            ),
            (
                "std_fs_whole_file_read_chunk_bytes",
                32768,
                "chunk must be exactly 65536 bytes",
            ),
            (
                "std_fs_whole_file_read_limit_eof",
                "trust metadata size",
                "must record limit-plus-one and EOF semantics",
            ),
            (
                "std_fs_whole_file_read_terminal_acknowledgement",
                "publish after the final byte",
                "must wait for descriptor close and handle root cleanup",
            ),
        )

        for field, replacement, diagnostic in mutations:
            with self.subTest(field=field):
                value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
                implementation = value["hosted_native_async"]["dispatch_io_payload_adapter"][
                    "implementation"
                ]
                implementation[field] = replacement
                result = self.run_target_with_manifest(value)
                self.assertEqual(result.returncode, 1)
                self.assertIn(diagnostic, result.stderr)

    def test_executor_requires_native_io_task_bridge(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["executor"]["implementation"][
            "native_io_task_bridge"
        ] = False
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must record the implemented native I/O task bridge", result.stderr)

    def test_executor_resumable_primitive_matches_runtime_abi(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["hosted_native_async"]["executor"]["implementation"][
            "resumable_computation_primitive"
        ]["await_api"] = "r_runtime_task_await"
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "executor resumable computation primitive does not match the runtime ABI",
            result.stderr,
        )

    def test_draft_target_cannot_claim_conformance(self) -> None:
        value = json.loads(TARGET_MANIFEST.read_text(encoding="utf-8"))
        value["conformance_claim"] = True
        result = self.run_target_with_manifest(value)
        self.assertEqual(result.returncode, 1)
        self.assertIn("must not claim implementation conformance", result.stderr)


if __name__ == "__main__":
    unittest.main()
