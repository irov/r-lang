#!/usr/bin/env python3
"""Regression tests for the Standard Library inventory and layout gates."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
LAYOUT_CHECKER = REPOSITORY_ROOT / "tools/check_library_layout.py"
INVENTORY_GENERATOR = REPOSITORY_ROOT / "tools/generate_library_inventory.py"
SPECIFICATION = REPOSITORY_ROOT / "specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc"
INVENTORY = REPOSITORY_ROOT / "library/generated/api_inventory/implementation_inventory.json"

sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))
from generate_library_inventory import (
    CANONICAL_SOURCE_SIGNATURE_IDS,
    CHECKED_SCHEMA_KERNEL_CONTRACT,
    COLLIDING_ITEM_FACETS,
    COMPLEX_FALLIBLE_BINARY,
    COMPLEX_FALLIBLE_UNARY,
    COMPLEX_NON_FAILING_BINARY,
    COMPLEX_NON_FAILING_UNARY,
    COMPLEX_SUFFIXES,
    CORE_INTRINSIC_IMPLEMENTATIONS,
    EXPLICIT_RULE_ITEMS,
    ITEM_FACETS,
    MATH_COMPLEX_SCHEMA_IMPLEMENTATIONS,
    MATH_COMPLEX_TYPE_IMPLEMENTATIONS,
    MATH_FALLIBLE_SCHEMA_IMPLEMENTATIONS,
    MATH_IMPLEMENTED_FALLIBLE_UNARY,
    MATH_IMPLEMENTED_NON_FAILING_SUFFIXES,
    MATH_NON_FAILING_BINARY,
    MATH_NON_FAILING_SCHEMA_IMPLEMENTATIONS,
    MATH_NON_FAILING_UNARY,
    MATH_PARTS_C_SUFFIX_NAMES,
    MATH_PARTS_OPERATIONS,
    MATH_PARTS_SCHEMA_IMPLEMENTATIONS,
    MATH_PARTS_TYPE_IMPLEMENTATIONS,
    MATH_PARTS_TYPES,
    MATH_SCALAR_SUFFIXES,
    SCHEMA_SPECIALIZATIONS,
    STD_C_IMPLEMENTATIONS,
    STD_ASYNC_IMPLEMENTATIONS,
    STD_CONVERT_IMPLEMENTATIONS,
    STD_SYNC_IMPLEMENTATIONS,
    SUPPLEMENTAL_RULE_ITEM_ATTRIBUTIONS,
    build_inventory,
    collect_normative_items,
    collect_qualified_callable_signatures,
    facet_for_item_kind,
    make_record_id,
    normalize_checked_signature,
    qualified_callable_signature_item,
    require_current_source_signature,
    checked_effect_contract,
    STANDARD_ERROR_ROOTS,
    standard_error_types,
    r_source_declaration,
)


def run_tool(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def inventory_records(inventory: dict[str, object]) -> dict[str, dict[str, object]]:
    items = inventory["items"]
    assert isinstance(items, list)
    return {
        record["record_id"]: record
        for record in items
        if isinstance(record, dict) and isinstance(record.get("record_id"), str)
    }


def inventory_record(
    inventory: dict[str, object], item_id: str, facet: str
) -> dict[str, object]:
    return inventory_records(inventory)[make_record_id(item_id, facet)]


class LibraryInventoryTests(unittest.TestCase):
    def make_repository_fixture(self, temporary_root: Path) -> None:
        shutil.copytree(REPOSITORY_ROOT / "library", temporary_root / "library")
        (temporary_root / "specification").mkdir()
        shutil.copy2(SPECIFICATION, temporary_root / "specification" / SPECIFICATION.name)

    def test_explicit_standard_error_registry(self) -> None:
        inventory = json.loads(INVENTORY.read_text())
        errors = standard_error_types()
        types = {item["id"]: item for item in inventory["items"] if item["facet"] == "type"}
        # R-SLIB-ERR-0004: the root of the standard errors is checked but never an exact error.
        self.assertEqual(
            {name for name, item in types.items()
             if item["checked_error"] and item["implementation"]["kind"] != "r_source"},
            set(errors.keys()) | STANDARD_ERROR_ROOTS,
        )
        self.assertTrue(STANDARD_ERROR_ROOTS.isdisjoint(errors.keys()))
        for name, arity in errors.items():
            self.assertEqual(types[name]["checked_error_arity"], arity)
        self.assertFalse(types["std.fs::error_code"]["checked_error"])
        self.assertTrue(types["std.dict::insert_error"]["checked_error"])
        self.assertEqual(errors["std.dict::insert_error"], 2)

    def test_discardable_results_follow_library_rule_26(self) -> None:
        inventory = json.loads(INVENTORY.read_text())
        flagged = {
            item["id"]
            for item in inventory["items"]
            if item["facet"] == "operation" and item["discardable_result"]
        }
        self.assertEqual(
            flagged,
            {
                "core::replace", "core::atomic_exchange", "core::atomic_fetch_add",
                "core::atomic_fetch_sub", "core::atomic_fetch_and", "core::atomic_fetch_or",
                "core::atomic_fetch_xor", "std.array::pop", "std.array::remove",
                "std.list::pop_front", "std.list::pop_back", "std.list::remove",
                "std.dict::insert", "std.dict::remove", "std.fs::seek", "std.sync::barrier_wait",
            },
        )
        for item in inventory["items"]:
            if item["facet"] == "operation" and item["implementation"]["kind"] == "r_source":
                self.assertFalse(item["discardable_result"], item["id"])
        # An argument-free attribute between the generic header and the declaration keeps arity.
        self.assertEqual(
            r_source_declaration(
                "@generic<T: key>\n@discardable\nbool insert(T value) { return true; }", "insert"
            ),
            ("function", 1),
        )

    def test_source_errors_use_declarations_and_no_native_abi(self) -> None:
        self.assertEqual(r_source_declaration("error failure { i32 code; };", "failure"), ("error", 0))
        self.assertEqual(r_source_declaration("@generic<T>\nerror failure { T value; };", "failure"), ("error", 1))
        inventory = json.loads(INVENTORY.read_text())
        error = inventory_record(inventory, "std.regex::error", "type")
        self.assertTrue(error["checked_error"])
        self.assertEqual(error["checked_error_arity"], 0)
        self.assertEqual(error["implementation"]["kind"], "r_source")
        self.assertNotIn("c_symbol", error["implementation"])
        code = inventory_record(inventory, "std.regex::error_code", "type")
        self.assertFalse(code["checked_error"])
        module = next(module for module in inventory["modules"] if module["r_module"] == "std.regex")
        self.assertEqual(module["minimum_profile"], "hosted")

    def test_repository_inventory_is_current_and_layout_is_valid(self) -> None:
        generation = run_tool(
            str(INVENTORY_GENERATOR),
            "--specification",
            str(SPECIFICATION),
            "--inventory",
            str(INVENTORY),
            "--verify",
        )
        self.assertEqual(generation.returncode, 0, generation.stderr)

        layout = run_tool(str(LAYOUT_CHECKER), "--root", str(REPOSITORY_ROOT))
        self.assertEqual(layout.returncode, 0, layout.stderr)
        self.assertIn("70 modules", layout.stdout)
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        r_modules = [
            module
            for module in inventory["modules"]
            if module.get("implementation_language") == "r"
        ]
        self.assertEqual(len(inventory["modules"]), 70)
        self.assertEqual(len(r_modules), 39)
        for module in r_modules:
            self.assertTrue((REPOSITORY_ROOT / module["source"]).is_file(), module)
            self.assertIn(
                module["minimum_profile"],
                {"freestanding", "allocation", "hosted", "hosted-native-async"},
            )
        unimplemented_count = sum(
            record["implementation"]["kind"] == "unimplemented"
            for record in inventory["items"]
        )
        partial_count = sum(
            record["implementation"].get("conformance_status") == "partial"
            for record in inventory["items"]
        )
        self.assertEqual(partial_count, 0)
        # Defect L45-5: the release gate requires every public record to be implemented.
        self.assertEqual(unimplemented_count, 0)
        self.assertIn(
            f"{unimplemented_count} unimplemented, {partial_count} partial", layout.stdout
        )

    def test_supplemental_rule_attributions_cover_unqualified_contracts(self) -> None:
        expected_attributions = {
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
        expected_record_rules = {
            # std.tls::config::set_identity (R-SLIB-TLS-0003) takes its private key as a buffer,
            # std.crypto keeps secret keys and shared secrets in one (M28) and returns the DER
            # and PEM of a private key in one (M36), and std.oauth2 keeps a client secret in one
            # (M38).
            ("std.secret::buffer", "type"): {
                "R-SLIB-SECRET-0001",
                "R-SLIB-SECRET-0003",
                "R-SLIB-TLS-0003",
                "R-SLIB-CRYPTO-0001",
                "R-SLIB-CRYPTO-0005",
                "R-SLIB-CRYPTO-0012",
                "R-SLIB-OAUTH2-0003",
            },
            # HMAC (R-SLIB-BYTES-0011) erases its padded keys with zeroize and directs callers
            # to constant_time_equal, so both operations cite that rule as well.
            ("std.secret::zeroize", "operation"): {
                "R-SLIB-BYTES-0011",
                "R-SLIB-SECRET-0001",
                "R-SLIB-SECRET-0003",
            },
            ("std.secret::constant_time_equal", "operation"): {
                "R-SLIB-BYTES-0011",
                "R-SLIB-SECRET-0001",
                "R-SLIB-SECRET-0004",
            },
            ("std.bits::read", "operation"): {
                "R-SLIB-BITS-0002",
                "R-SLIB-BITS-0003",
            },
            ("std.bits::align_byte", "operation"): {
                "R-SLIB-BITS-0002",
                "R-SLIB-BITS-0004",
            },
            ("std.utf8::is_valid", "operation"): {
                "R-SLIB-CONF-D002",
                "R-SLIB-UTF8-0001",
                "R-SLIB-UTF8-0002",
            },
            ("std.utf8::validate", "operation"): {
                "R-SLIB-CONF-D002",
                "R-SLIB-UTF8-0001",
                "R-SLIB-UTF8-0002",
            },
            ("std.hash::md5", "operation"): {
                "R-SLIB-BYTES-0005",
                "R-SLIB-BYTES-0007",
                "R-SLIB-BYTES-0008",
            },
            ("std.hash::md5_digest", "type"): {
                "R-SLIB-GEN-0009",
                "R-SLIB-BYTES-0005",
                "R-SLIB-BYTES-0007",
            },
        }
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))

        self.assertEqual(SUPPLEMENTAL_RULE_ITEM_ATTRIBUTIONS, expected_attributions)
        self.assertEqual(
            inventory["extraction"]["supplemental_rule_item_attributions"],
            [
                {"rule": rule_id, "items": list(item_ids)}
                for rule_id, item_ids in sorted(expected_attributions.items())
            ],
        )
        for (item_id, facet), expected_rules in expected_record_rules.items():
            self.assertEqual(
                set(inventory_record(inventory, item_id, facet)["normative_rules"]),
                expected_rules,
            )

    def test_release_coverage_gate_rejects_an_unimplemented_item(self) -> None:
        value = json.loads(INVENTORY.read_text(encoding="utf-8"))
        for record in value["items"]:
            if record["implementation"]["kind"] == "unimplemented":
                record["implementation"] = {
                    "kind": "generated",
                    "generator_contract": "complete-gate test fixture",
                }
            record["implementation"].pop("conformance_status", None)
        deliberately_missing = inventory_record(value, "core::min_SUFFIX", "constant")
        deliberately_missing["implementation"] = {"kind": "unimplemented"}

        with tempfile.TemporaryDirectory() as temporary_directory:
            inventory = Path(temporary_directory) / "inventory.json"
            inventory.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
            incomplete = run_tool(
                str(LAYOUT_CHECKER),
                "--root",
                str(REPOSITORY_ROOT),
                "--inventory",
                str(inventory),
                "--require-complete",
            )

        self.assertEqual(incomplete.returncode, 1)
        self.assertIn("1 public item records remain unimplemented", incomplete.stderr)
        self.assertIn("core::min_SUFFIX", incomplete.stderr)

    def test_release_coverage_gate_rejects_partial_items(self) -> None:
        value = json.loads(INVENTORY.read_text(encoding="utf-8"))
        for record in value["items"]:
            if record["implementation"]["kind"] == "unimplemented":
                record["implementation"] = {
                    "kind": "generated",
                    "generator_contract": "complete-gate test fixture",
                }
        inventory_record(value, "core::adopt", "operation")["implementation"][
            "conformance_status"
        ] = "partial"
        inventory_record(value, "core::release", "operation")["implementation"][
            "conformance_status"
        ] = "partial"

        with tempfile.TemporaryDirectory() as temporary_directory:
            inventory = Path(temporary_directory) / "inventory.json"
            inventory.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
            incomplete = run_tool(
                str(LAYOUT_CHECKER),
                "--root",
                str(REPOSITORY_ROOT),
                "--inventory",
                str(inventory),
                "--require-complete",
            )

        self.assertEqual(incomplete.returncode, 1)
        self.assertIn("2 public item records remain partial", incomplete.stderr)
        self.assertIn("core::adopt#operation", incomplete.stderr)
        self.assertIn("core::release#operation", incomplete.stderr)

    def test_layout_validates_partial_conformance_metadata(self) -> None:
        for mutation, expected_error in (
            ("unknown_status", "unknown conformance status unfinished"),
            ("missing_contract", "requires a nonempty generator_contract"),
        ):
            with self.subTest(mutation=mutation):
                value = json.loads(INVENTORY.read_text(encoding="utf-8"))
                checked = inventory_record(value, "core::adopt", "operation")
                if mutation == "unknown_status":
                    checked["implementation"]["conformance_status"] = "unfinished"
                else:
                    checked["implementation"]["conformance_status"] = "partial"
                    checked["implementation"].pop("generator_contract")

                with tempfile.TemporaryDirectory() as temporary_directory:
                    inventory = Path(temporary_directory) / "inventory.json"
                    inventory.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
                    result = run_tool(
                        str(LAYOUT_CHECKER),
                        "--root",
                        str(REPOSITORY_ROOT),
                        "--inventory",
                        str(inventory),
                    )

                self.assertEqual(result.returncode, 1)
                self.assertIn(expected_error, result.stderr)

    def test_explicit_schemas_and_imported_core_intrinsics_are_complete(self) -> None:
        specification_text = SPECIFICATION.read_text(encoding="utf-8")
        normative_items = collect_normative_items(specification_text)
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)

        self.assertEqual(len(records), len(inventory["items"]))
        for rule_id, family in EXPLICIT_RULE_ITEMS.items():
            for explicit_item in family:
                self.assertIn(explicit_item.item_id, normative_items)
                self.assertIn(rule_id, normative_items[explicit_item.item_id])
                facet = facet_for_item_kind(explicit_item.item_id, explicit_item.item_kind)
                self.assertIn(make_record_id(explicit_item.item_id, facet), records)

        imported_core_intrinsics = {
            "core::slice_from_raw_parts",
            "core::slice_from_raw_parts_mut",
            "core::slice_from_raw_parts_in",
            "core::slice_from_raw_parts_in_mut",
            "core::volatile_load",
            "core::volatile_store",
            "core::assume",
            "core::adopt",
            "core::release",
        }
        self.assertLessEqual(
            {make_record_id(item_id, "operation") for item_id in imported_core_intrinsics},
            records.keys(),
        )
        for item_id in (
            "core::assume",
            "core::volatile_load",
            "core::volatile_store",
        ):
            self.assertEqual(
                records[make_record_id(item_id, "operation")]["implementation"],
                CORE_INTRINSIC_IMPLEMENTATIONS[item_id]["implementation"],
            )

        for operation in MATH_IMPLEMENTED_FALLIBLE_UNARY:
            family_id = f"std.math::{operation}_S"
            family = records[make_record_id(family_id, "operation")]
            self.assertEqual(
                family["implementation"],
                MATH_FALLIBLE_SCHEMA_IMPLEMENTATIONS[family_id]["implementation"],
            )
            for suffix in MATH_SCALAR_SUFFIXES:
                item_id = f"std.math::{operation}_{suffix}"
                specialization = records[make_record_id(item_id, "operation")]
                self.assertEqual(specialization["item_kind"], "operation_specialization")
                self.assertEqual(specialization["schema_family"], family_id)
                self.assertEqual(
                    specialization["implementation"],
                    {
                        "kind": "source",
                        "source": f"library/std/math/source/{operation}_{suffix}.c",
                        "c_symbol": f"r_std_math_{operation}_{suffix}",
                    },
                )
                self.assertEqual(
                    specialization["normative_rules"],
                    ["R-SLIB-MATH-0002", "R-SLIB-MATH-0003", "R-SLIB-MATH-0006"],
                )
        for suffix in MATH_IMPLEMENTED_NON_FAILING_SUFFIXES:
            for operation in (*MATH_NON_FAILING_UNARY, *MATH_NON_FAILING_BINARY):
                item_id = f"std.math::{operation}_{suffix}"
                specialization = records[make_record_id(item_id, "operation")]
                self.assertEqual(
                    specialization["schema_family"],
                    f"std.math::{operation}_S",
                )
                self.assertEqual(
                    specialization["implementation"],
                    {
                        "kind": "source",
                        "source": f"library/std/math/source/{operation}_{suffix}.c",
                        "c_symbol": f"r_std_math_{operation}_{suffix}",
                    },
                )
        for operation in (*MATH_NON_FAILING_UNARY, *MATH_NON_FAILING_BINARY):
            item_id = f"std.math::{operation}_S"
            self.assertEqual(
                records[make_record_id(item_id, "operation")]["implementation"],
                MATH_NON_FAILING_SCHEMA_IMPLEMENTATIONS[item_id]["implementation"],
            )
        for suffix in MATH_SCALAR_SUFFIXES:
            for parts_type in MATH_PARTS_TYPES:
                item_id = f"std.math::{parts_type}_{suffix}"
                specialization = records[make_record_id(item_id, "type")]
                self.assertEqual(
                    specialization["schema_family"],
                    f"std.math::{parts_type}_S",
                )
                self.assertEqual(
                    specialization["implementation"],
                    MATH_PARTS_TYPE_IMPLEMENTATIONS[item_id]["implementation"],
                )
                self.assertEqual(
                    specialization["implementation"]["c_type"],
                    f"RStdMath{''.join(word.title() for word in parts_type.split('_'))}"
                    f"{MATH_PARTS_C_SUFFIX_NAMES[suffix]}",
                )
            for operation in MATH_PARTS_OPERATIONS:
                item_id = f"std.math::{operation}_{suffix}"
                specialization = records[make_record_id(item_id, "operation")]
                self.assertEqual(
                    specialization["schema_family"],
                    f"std.math::{operation}_S",
                )
                self.assertEqual(
                    specialization["implementation"],
                    {
                        "kind": "source",
                        "source": f"library/std/math/source/{operation}_{suffix}.c",
                        "c_symbol": f"r_std_math_{operation}_{suffix}",
                    },
                )
        for schema in (*MATH_PARTS_TYPES, *MATH_PARTS_OPERATIONS):
            item_id = f"std.math::{schema}_S"
            self.assertEqual(
                records[
                    make_record_id(
                        item_id,
                        "type" if schema in MATH_PARTS_TYPES else "operation",
                    )
                ]["implementation"],
                MATH_PARTS_SCHEMA_IMPLEMENTATIONS[item_id]["implementation"],
            )
        complex_operations = (
            *COMPLEX_NON_FAILING_BINARY,
            *COMPLEX_NON_FAILING_UNARY,
            "magnitude",
            *COMPLEX_FALLIBLE_UNARY,
            *COMPLEX_FALLIBLE_BINARY,
        )
        for suffix in COMPLEX_SUFFIXES:
            type_id = f"std.math::{suffix}"
            self.assertEqual(
                records[make_record_id(type_id, "type")]["implementation"],
                MATH_COMPLEX_TYPE_IMPLEMENTATIONS[type_id]["implementation"],
            )
            for operation in complex_operations:
                item_id = f"std.math::{operation}_{suffix}"
                specialization = records[make_record_id(item_id, "operation")]
                self.assertEqual(
                    specialization["schema_family"],
                    f"std.math::{operation}_C",
                )
                self.assertEqual(
                    specialization["implementation"],
                    {
                        "kind": "source",
                        "source": f"library/std/math/source/{operation}_{suffix}.c",
                        "c_symbol": f"r_std_math_{operation}_{suffix}",
                    },
                )
        for operation in complex_operations:
            item_id = f"std.math::{operation}_C"
            self.assertEqual(
                records[make_record_id(item_id, "operation")]["implementation"],
                MATH_COMPLEX_SCHEMA_IMPLEMENTATIONS[item_id]["implementation"],
            )

    def test_record_identity_preserves_spelling_and_splits_closed_collisions(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)

        self.assertEqual(inventory["schema_version"], 3)
        self.assertEqual(len(records), len(inventory["items"]))
        self.assertEqual(
            [record["record_id"] for record in inventory["items"]],
            sorted(records),
        )
        for record in inventory["items"]:
            self.assertIn(record["facet"], ITEM_FACETS)
            self.assertEqual(
                record["record_id"],
                make_record_id(record["id"], record["facet"]),
            )
            self.assertNotIn("_and_", record["item_kind"])
            if record["implementation"]["kind"] == "r_source":
                # A standard module written in R has no C symbol (Library R-SLIB-RSRC-0001):
                # the record names its source and R symbol and carries no ABI descriptor.
                self.assertTrue(record["implementation"]["source"].startswith("library/r/"))
                self.assertEqual(record["implementation"]["r_symbol"], record["id"])
                self.assertNotIn("checked_effect", record)
                continue
            if record["facet"] == "operation":
                self.assertIn("source_signature", record)
                self.assertNotRegex(record["source_signature"], r"(?<![A-Za-z0-9_])r\(")
                self.assertNotRegex(
                    record["source_signature"], r"(?<![A-Za-z0-9_])r::(?:ok|err)\b"
                )
                self.assertIn("checked_effect", record)
                checked_effect = record["checked_effect"]
                self.assertEqual(checked_effect["tag_table"][0]["tag"], 0)
                self.assertEqual(checked_effect["tag_table"][0]["role"], "success")
                self.assertEqual(
                    checked_effect["normalized_error_set"],
                    sorted(checked_effect["normalized_error_set"]),
                )
                self.assertRegex(checked_effect["descriptor_sha256"], r"^[0-9a-f]{64}$")

        for item_id, definitions in COLLIDING_ITEM_FACETS.items():
            collision = [record for record in inventory["items"] if record["id"] == item_id]
            self.assertEqual(
                {record["facet"] for record in collision},
                set(definitions),
            )
            self.assertEqual({record["id"] for record in collision}, {item_id})

        dict_operation = records["std.dict::iter#operation"]
        dict_type = records["std.dict::iter#type"]
        self.assertEqual(dict_operation["implementation"]["kind"], "source")
        self.assertEqual(dict_operation["normative_rules"], ["R-LIB-0020"])
        self.assertEqual(dict_type["implementation"]["kind"], "header")
        self.assertEqual(dict_type["normative_rules"], ["R-LIB-0020", "R-LIB-0021"])

        list_operation = records["std.list::iter#operation"]
        list_type = records["std.list::iter#type"]
        self.assertEqual(list_operation["implementation"]["kind"], "source")
        self.assertEqual(list_operation["normative_rules"], ["R-LIB-0022"])
        self.assertEqual(list_type["implementation"]["kind"], "header")
        self.assertEqual(list_type["normative_rules"], ["R-LIB-0022", "R-LIB-0023"])

        metadata_operation = records["std.fs::metadata#operation"]
        metadata_type = records["std.fs::metadata#type"]
        self.assertEqual(
            metadata_operation["implementation"],
            {
                "kind": "source",
                "source": "library/std/fs/source/metadata.c",
                "c_symbol": "r_std_fs_metadata",
            },
        )

        self.assertIn("async std.fs::metadata(", metadata_operation["source_signature"])
        self.assertEqual(metadata_type["implementation"]["kind"], "header")

        read_result_type = records["std.io::read_result#type"]
        self.assertEqual(
            read_result_type["implementation"]["type_glue"],
            {
                "move_initialize": "r_std_io_read_result_move_initialize",
                "drop": "r_std_io_read_result_destroy",
                "linkage": "static_inline",
            },
        )
        write_result_type = records["std.io::write_result#type"]
        self.assertEqual(
            write_result_type["implementation"]["type_glue"],
            {
                "move_initialize": "r_std_io_write_result_move_initialize",
                "drop": "r_std_io_write_result_destroy",
                "linkage": "static_inline",
            },
        )

    def test_checked_effects_use_source_throws_and_named_outcomes(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)

        read_file = records["std.fs::read_file#operation"]
        self.assertEqual(
            read_file["checked_effect"]["normalized_error_set"],
            ["std.fs::fs_error"],
        )
        self.assertEqual(
            read_file["checked_effect"]["tag_table"],
            [
                {"tag": 0, "role": "success", "type": "array<u8>"},
                {"tag": 1, "role": "checked_error", "type": "std.fs::fs_error"},
            ],
        )
        self.assertEqual(read_file["checked_effect"]["declaration_kind"], "async")
        self.assertEqual(
            read_file["checked_effect"]["invocation"]["value_type"],
            "task<array<u8> throws std.fs::fs_error>",
        )
        self.assertEqual(
            read_file["checked_effect"]["invocation"]["normalized_error_set"],
            ["std.async::start_error"],
        )
        self.assertEqual(
            read_file["checked_effect"]["completion"]["normalized_error_set"],
            ["std.fs::fs_error"],
        )
        self.assertNotIn("r(", read_file["source_signature"])

        ordinary_outcomes = {
            "core::atomic_compare_exchange#operation": (
                "core::atomic_compare_exchange_result<T>"
            ),
            "std.arc::try_unwrap#operation": "std.arc::try_unwrap_result<T>",
            "std.rc::try_unwrap#operation": "std.rc::try_unwrap_result<T>",
        }
        for record_id, result_type in ordinary_outcomes.items():
            operation = records[record_id]
            self.assertEqual(operation["checked_effect"]["value_type"], result_type)
            self.assertEqual(operation["checked_effect"]["normalized_error_set"], [])
            self.assertEqual(operation["checked_effect"]["carrier"], "none")

        for record_id in (
            "core::atomic_compare_exchange_result#type",
            "std.arc::try_unwrap_result#type",
            "std.rc::try_unwrap_result#type",
        ):
            outcome = records[record_id]
            self.assertEqual(outcome["facet"], "type")
            self.assertNotEqual(outcome["implementation"]["kind"], "unimplemented")

    def test_every_checked_effect_descriptor_has_canonical_tags(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))

        def assert_carrier(contract: dict, expected_carrier: str) -> None:
            errors = contract["normalized_error_set"]
            tags = contract["tag_table"]

            self.assertEqual(errors, sorted(set(errors)))
            self.assertEqual([entry["tag"] for entry in tags], list(range(len(tags))))
            self.assertEqual(
                tags,
                [{"tag": 0, "role": "success", "type": contract["value_type"]}]
                + [
                    {"tag": index, "role": "checked_error", "type": error_type}
                    for index, error_type in enumerate(errors, start=1)
                ],
            )
            self.assertEqual(
                contract["carrier"], expected_carrier if errors else "none"
            )

        for record in inventory["items"]:
            if record["facet"] != "operation":
                continue
            if record["implementation"]["kind"] == "r_source":
                continue
            checked_effect = record["checked_effect"]
            assert_carrier(checked_effect, "explicit_output_parameter")
            assert_carrier(
                checked_effect["invocation"], "explicit_output_parameter"
            )
            if checked_effect["declaration_kind"] == "async":
                self.assertEqual(
                    checked_effect["invocation"]["normalized_error_set"],
                    ["std.async::start_error"],
                )
                self.assertIsNotNone(checked_effect["completion"])
                assert_carrier(checked_effect["completion"], "task_completion")
            else:
                self.assertIsNone(checked_effect["completion"])

    def test_legacy_result_source_signature_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, r"legacy r\(T,E\)"):
            require_current_source_signature(
                "std.fs::read_file",
                "async std.fs::read_file(std.fs::path path) -> "
                "r(array<u8>, std.fs::fs_error)",
            )

        require_current_source_signature(
            "std.fs::read_file",
            "async std.fs::read_file(std.fs::path path) -> "
            "array<u8> throws std.fs::fs_error",
        )

    def test_nested_task_effect_is_distinct_from_immediate_effect(self) -> None:
        effect = checked_effect_contract(
            "std.thread::spawn",
            "std.thread::spawn(F entry) -> "
            "std.thread::join_handle<T throws application::entry_error> "
            "throws std.thread::thread_error",
        )
        self.assertEqual(
            effect["value_type"],
            "std.thread::join_handle<T throws application::entry_error>",
        )
        self.assertEqual(
            effect["normalized_error_set"],
            ["std.thread::thread_error"],
        )
        self.assertEqual(effect["declaration_kind"], "synchronous")
        self.assertEqual(effect["invocation"]["normalized_error_set"],
                         ["std.thread::thread_error"])
        self.assertIsNone(effect["completion"])

        completion_only = checked_effect_contract(
            "std.async::identity",
            "std.async::identity() -> task<T throws application::entry_error>",
        )
        self.assertEqual(completion_only["normalized_error_set"], [])
        self.assertEqual(completion_only["carrier"], "none")

    def test_effect_polymorphic_thread_and_once_operations_are_preserved(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)

        expected = {
            "std.thread::join": ("std.thread::join_result<R>", ["E..."]),
            "std.sync::call_once": ("void", ["E..."]),
            "std.sync::call_once_force": ("void", ["E..."]),
            "std.sync::get_or_init": ("const T*", ["E..."]),
        }
        for item_id, (value_type, errors) in expected.items():
            record = records[make_record_id(item_id, "operation")]
            self.assertEqual(record["item_kind"], "operation_schema")
            self.assertEqual(record["checked_effect"]["value_type"], value_type)
            self.assertEqual(record["checked_effect"]["normalized_error_set"], errors)
            self.assertEqual(
                record["checked_effect"]["carrier"], "explicit_output_parameter"
            )

        for item_id, handle_type in (
            ("std.thread::spawn", "std.thread::join_handle<R throws E...>"),
            (
                "std.thread::spawn_scoped",
                "std.thread::scoped_join_handle<R throws E...>",
            ),
        ):
            effect = records[make_record_id(item_id, "operation")]["checked_effect"]
            self.assertEqual(effect["value_type"], handle_type)
            self.assertEqual(
                effect["normalized_error_set"], ["std.thread::thread_error"]
            )

    def test_qualified_callable_signatures_resolve_to_operation_facets(self) -> None:
        specification_text = SPECIFICATION.read_text(encoding="utf-8")
        signatures = collect_qualified_callable_signatures(specification_text)
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)

        for item_id in signatures:
            self.assertIn(make_record_id(item_id, "operation"), records)
        self.assertEqual(
            qualified_callable_signature_item(
                "std.error::name(std.error::error value) -> std.error::error"
            ),
            "std.error::name",
        )
        self.assertIsNone(
            qualified_callable_signature_item(
                "std.error::error returned by std.error::name(value)"
            )
        )
        self.assertEqual(
            qualified_callable_signature_item(
                "std.c::callback(raw fn(raw void*) -> void value) -> void"
            ),
            "std.c::callback",
        )

    def test_new_type_operation_collision_is_discovered_from_signature(self) -> None:
        specification_text = SPECIFICATION.read_text(encoding="utf-8")
        specification_text += (
            "\n[[R-SLIB-TEST-9998]]\n"
            "*R-SLIB-TEST-9998* — Exact operation is "
            "`+std.alloc::alloc_error() -> std.alloc::alloc_error+`.\n"
        )
        existing = json.loads(INVENTORY.read_text(encoding="utf-8"))

        with tempfile.TemporaryDirectory() as temporary_directory:
            specification = Path(temporary_directory) / SPECIFICATION.name
            specification.write_text(specification_text, encoding="utf-8")
            generated = build_inventory(specification, existing)
            regenerated = build_inventory(specification, generated)

        operation = inventory_record(generated, "std.alloc::alloc_error", "operation")
        type_record = inventory_record(generated, "std.alloc::alloc_error", "type")
        self.assertEqual(operation["normative_rules"], ["R-SLIB-TEST-9998"])
        self.assertEqual(
            operation["source_signature"],
            "std.alloc::alloc_error() -> std.alloc::alloc_error",
        )
        self.assertEqual(operation["implementation"], {"kind": "unimplemented"})
        self.assertEqual(type_record["implementation"]["kind"], "header")
        self.assertEqual(generated, regenerated)

    def test_checked_conversion_schema_kernels_have_canonical_mappings(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)
        expected_items = (
            ("std.convert::checked_D", STD_CONVERT_IMPLEMENTATIONS["std.convert::checked_D"]),
            ("std.c::checked_D", STD_C_IMPLEMENTATIONS["std.c::checked_D"]),
        )

        self.assertEqual(
            set(STD_C_IMPLEMENTATIONS),
            {
                "std.c::c_string",
                "std.c::checked_D",
                "std.c::handle",
                "std.c::link_available",
                "std.c::thread_attachment",
            },
        )
        for item_id, canonical in expected_items:
            record = records[make_record_id(item_id, "operation")]
            self.assertEqual(record["item_kind"], "operation_schema")
            self.assertEqual(
                record["source_signature"],
                normalize_checked_signature(item_id, canonical["source_signature"]),
            )
            self.assertEqual(record["implementation"], canonical["implementation"])
            self.assertEqual(
                record["implementation"]["generator_contract"],
                CHECKED_SCHEMA_KERNEL_CONTRACT,
            )
            self.assertNotIn("conformance_status", record["implementation"])
            self.assertTrue(record["implementation"]["filename_exception"])
            self.assertIn("Target-manifest-driven ABI generation", CHECKED_SCHEMA_KERNEL_CONTRACT)
            self.assertIn("complete source/destination matrix", CHECKED_SCHEMA_KERNEL_CONTRACT)
            self.assertIn("exact selected target-manifest identity", CHECKED_SCHEMA_KERNEL_CONTRACT)

    def test_link_available_has_canonical_source_mapping(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        record = inventory_records(inventory)[
            make_record_id("std.c::link_available", "operation")
        ]
        canonical = STD_C_IMPLEMENTATIONS["std.c::link_available"]

        self.assertEqual(record["item_kind"], "operation")
        self.assertEqual(record["source_signature"], canonical["source_signature"])
        self.assertEqual(record["implementation"], canonical["implementation"])
        self.assertEqual(
            record["implementation"]["source"],
            "library/std/c/source/link_available.c",
        )
        self.assertEqual(
            record["implementation"]["c_symbol"],
            "r_std_c_link_available",
        )

    def test_explicit_rule_change_requires_catalog_audit(self) -> None:
        specification_text = SPECIFICATION.read_text(encoding="utf-8")
        mutated_text = specification_text.replace(
            "For each S in F, `+std.math+` provides these exact non-failing operations:",
            "For every S in F, `+std.math+` provides these exact non-failing operations:",
            1,
        )
        self.assertNotEqual(mutated_text, specification_text)
        with self.assertRaisesRegex(
            ValueError,
            "explicit public-item rule changed and requires catalog audit: R-SLIB-MATH-0002",
        ):
            collect_normative_items(mutated_text)

    def test_sync_slice_has_canonical_mappings(self) -> None:
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)

        self.assertEqual(set(STD_SYNC_IMPLEMENTATIONS), {
            "std.sync::barrier",
            "std.sync::barrier_error",
            "std.sync::barrier_new",
            "std.sync::barrier_wait",
            "std.sync::barrier_wait_result",
            "std.sync::call_once",
            "std.sync::call_once_force",
            "std.sync::channel",
            "std.sync::clone_sender",
            "std.sync::clone_sync_sender",
            "std.sync::condvar",
            "std.sync::condvar_new",
            "std.sync::get",
            "std.sync::get_or_init",
            "std.sync::lock",
            "std.sync::lock_result",
            "std.sync::mutex",
            "std.sync::mutex_guard",
            "std.sync::mutex_guard_mut",
            "std.sync::mutex_guard_ref",
            "std.sync::mutex_new",
            "std.sync::notify_all",
            "std.sync::notify_one",
            "std.sync::once",
            "std.sync::once_lock",
            "std.sync::once_new",
            "std.sync::read",
            "std.sync::read_lock_result",
            "std.sync::receive",
            "std.sync::receiver",
            "std.sync::recv",
            "std.sync::recv_result",
            "std.sync::reserve",
            "std.sync::reserve_result",
            "std.sync::rw_lock",
            "std.sync::rw_read_guard",
            "std.sync::rw_read_guard_ref",
            "std.sync::rw_write_guard",
            "std.sync::rw_write_guard_mut",
            "std.sync::rw_write_guard_ref",
            "std.sync::rwlock_new",
            "std.sync::permit",
            "std.sync::send",
            "std.sync::send_permit",
            "std.sync::send_result",
            "std.sync::sender",
            "std.sync::set",
            "std.sync::set_result",
            "std.sync::sync_channel",
            "std.sync::sync_receiver",
            "std.sync::sync_send",
            "std.sync::sync_sender",
            "std.sync::try_lock",
            "std.sync::try_lock_result",
            "std.sync::try_read",
            "std.sync::try_read_lock_result",
            "std.sync::try_recv",
            "std.sync::try_recv_result",
            "std.sync::try_reserve",
            "std.sync::try_reserve_result",
            "std.sync::try_send",
            "std.sync::try_send_result",
            "std.sync::try_write",
            "std.sync::try_write_lock_result",
            "std.sync::unlock",
            "std.sync::wait",
            "std.sync::write",
            "std.sync::write_lock_result",
        })
        for item_id, canonical in STD_SYNC_IMPLEMENTATIONS.items():
            facet = facet_for_item_kind(item_id, canonical["item_kind"])
            record = records[make_record_id(item_id, facet)]
            self.assertEqual(record["item_kind"], canonical["item_kind"])
            self.assertEqual(record["implementation"], canonical["implementation"])

    def test_async_sync_slice_has_canonical_mappings(self) -> None:
        # R-SLIB-ASYNC-0013..0018, R-SLIB-ASYNC-0020 (L30, L31, M23, L39): locks, semaphore,
        # notify, broadcast, blocking calls, the task identifier and the join of a task.
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        records = inventory_records(inventory)
        names = {
            "mutex", "mutex_guard", "mutex_new", "clone_mutex", "lock", "try_lock",
            "mutex_guard_ref", "mutex_guard_mut", "unlock", "rw_lock", "rw_read_guard",
            "rw_write_guard", "rwlock_new", "clone_rw_lock", "read", "write", "try_read",
            "try_write", "rw_read_guard_ref", "rw_write_guard_ref", "rw_write_guard_mut",
            "semaphore", "semaphore_permit", "semaphore_new", "clone_semaphore", "acquire",
            "try_acquire", "release", "add_permits", "available_permits", "notify",
            "notify_new", "clone_notify", "notify_one", "notify_all", "notified", "broadcast",
            "broadcast_receiver", "broadcast_result", "clone_broadcast", "subscribe", "publish",
            "broadcast_receive", "blocking", "task_id", "join",
        }
        self.assertEqual(set(STD_ASYNC_IMPLEMENTATIONS), {f"std.async::{name}" for name in names})
        for item_id, canonical in STD_ASYNC_IMPLEMENTATIONS.items():
            facet = facet_for_item_kind(item_id, canonical["item_kind"])
            record = records[make_record_id(item_id, facet)]
            self.assertEqual(record["item_kind"], canonical["item_kind"])
            self.assertEqual(record["implementation"], canonical["implementation"])

    def test_rwlock_slice_is_in_place_and_allocation_free(self) -> None:
        source_paths = [
            REPOSITORY_ROOT / "library/internal/synchronization/source/rwlock.c",
            *(
                REPOSITORY_ROOT / f"library/std/sync/source/{operation}.c"
                for operation in (
                    "read",
                    "rw_read_guard_ref",
                    "rw_write_guard_mut",
                    "rw_write_guard_ref",
                    "rwlock_new",
                    "try_read",
                    "try_write",
                    "write",
                )
            ),
        ]
        for source_path in source_paths:
            source_text = source_path.read_text(encoding="utf-8")
            for forbidden in ("malloc(", "calloc(", "realloc(", "r_runtime_allocator_"):
                self.assertNotIn(forbidden, source_text, source_path)

        header_text = (
            REPOSITORY_ROOT / "library/std/sync/include/r_std_sync.h"
        ).read_text(encoding="utf-8")
        self.assertIn("intrusive allocation-free reader record", header_text)
        self.assertIn("shall never be byte-copied", header_text)

    def test_new_unqualified_signature_requires_explicit_audit(self) -> None:
        specification_text = SPECIFICATION.read_text(encoding="utf-8")
        mutated_text = (
            specification_text
            + "\n[[R-SLIB-TEST-9999]]\n"
            + "*R-SLIB-TEST-9999* — Exact operation is `+missing_SCHEMA(T value) -> T+`.\n"
        )
        with self.assertRaisesRegex(
            ValueError,
            "unqualified public-schema candidate requires explicit audit: R-SLIB-TEST-9999",
        ):
            collect_normative_items(mutated_text)

    def test_regeneration_preserves_existing_classification_and_mapping(self) -> None:
        existing = json.loads(INVENTORY.read_text(encoding="utf-8"))
        expected = build_inventory(SPECIFICATION, existing)
        expected_by_record_id = inventory_records(expected)
        explicit_item_ids = {
            item.item_id for family in EXPLICIT_RULE_ITEMS.values() for item in family
        }

        for old_record in existing["items"]:
            new_record = expected_by_record_id.get(old_record["record_id"])
            if new_record is None:
                continue
            if (
                old_record["id"] not in SCHEMA_SPECIALIZATIONS
                and old_record["id"] not in explicit_item_ids
            ):
                self.assertEqual(new_record["item_kind"], old_record["item_kind"])
            self.assertEqual(new_record["implementation"], old_record["implementation"])
            if (
                "source_signature" in old_record
                and old_record["id"] not in CANONICAL_SOURCE_SIGNATURE_IDS
            ):
                self.assertEqual(new_record["source_signature"], old_record["source_signature"])

    def test_regeneration_preserves_implementation_by_record_identity(self) -> None:
        existing = json.loads(INVENTORY.read_text(encoding="utf-8"))
        specification_text = SPECIFICATION.read_text(encoding="utf-8")
        specification_text += (
            "\n[[R-SLIB-TEST-9998]]\n"
            "*R-SLIB-TEST-9998* — Exact operation is "
            "`+std.alloc::alloc_error() -> std.alloc::alloc_error+`.\n"
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            specification = Path(temporary_directory) / SPECIFICATION.name
            specification.write_text(specification_text, encoding="utf-8")
            generated = build_inventory(specification, existing)
            operation = inventory_record(generated, "std.alloc::alloc_error", "operation")
            operation["implementation"] = {
                "kind": "generated",
                "generator_contract": "faceted preservation test fixture",
            }
            regenerated = build_inventory(specification, generated)

        self.assertEqual(
            inventory_record(regenerated, "std.alloc::alloc_error", "operation")[
                "implementation"
            ],
            operation["implementation"],
        )
        self.assertEqual(
            inventory_record(regenerated, "std.alloc::alloc_error", "type")["implementation"],
            inventory_record(existing, "std.alloc::alloc_error", "type")["implementation"],
        )

    def test_schema_one_combined_records_migrate_to_separate_facets(self) -> None:
        current = json.loads(INVENTORY.read_text(encoding="utf-8"))
        legacy = json.loads(json.dumps(current))
        legacy["schema_version"] = 1
        legacy_items = []
        for record in legacy["items"]:
            item_id = record["id"]
            facet = record.pop("facet")
            record.pop("record_id")
            if item_id in {"std.dict::iter", "std.list::iter"}:
                if facet == "type":
                    continue
                type_record = inventory_record(current, item_id, "type")
                record["item_kind"] = "operation_and_public_type_schema"
                record["implementation"]["c_type"] = type_record["implementation"][
                    "c_type"
                ]
            elif item_id == "std.fs::metadata":
                if facet == "operation":
                    continue
            legacy_items.append(record)
        legacy["items"] = legacy_items

        migrated = build_inventory(SPECIFICATION, legacy)
        migrated_records = inventory_records(migrated)
        for item_id in COLLIDING_ITEM_FACETS:
            self.assertIn(make_record_id(item_id, "operation"), migrated_records)
            self.assertIn(make_record_id(item_id, "type"), migrated_records)
        self.assertEqual(
            migrated_records["std.dict::iter#operation"]["implementation"],
            COLLIDING_ITEM_FACETS["std.dict::iter"]["operation"]["implementation"],
        )
        self.assertEqual(
            migrated_records["std.dict::iter#type"]["implementation"],
            COLLIDING_ITEM_FACETS["std.dict::iter"]["type"]["implementation"],
        )

    def test_layout_rejects_record_identity_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            inventory_path = Path(temporary_directory) / "inventory.json"
            value = json.loads(INVENTORY.read_text(encoding="utf-8"))
            value["items"][0]["record_id"] = "core::wrong#operation"
            inventory_path.write_text(json.dumps(value), encoding="utf-8")

            layout = run_tool(
                str(LAYOUT_CHECKER),
                "--root",
                str(REPOSITORY_ROOT),
                "--inventory",
                str(inventory_path),
            )

        self.assertEqual(layout.returncode, 1)
        self.assertIn("identity must be", layout.stderr)

    def test_duplicate_item_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            inventory_path = Path(temporary_directory) / "inventory.json"
            value = json.loads(INVENTORY.read_text(encoding="utf-8"))
            value["items"].append(value["items"][0])
            inventory_path.write_text(json.dumps(value), encoding="utf-8")

            layout = run_tool(
                str(LAYOUT_CHECKER),
                "--root",
                str(REPOSITORY_ROOT),
                "--inventory",
                str(inventory_path),
            )
            self.assertEqual(layout.returncode, 1)
            self.assertIn("duplicate public item record", layout.stderr)

    def test_unregistered_module_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            self.make_repository_fixture(temporary_root)
            source_directory = temporary_root / "library/std/alloc/source"
            source_directory.mkdir(exist_ok=True)
            (source_directory / "all.c").write_text("typedef int RForbidden;\n", encoding="utf-8")

            layout = run_tool(str(LAYOUT_CHECKER), "--root", str(temporary_root))
            self.assertEqual(layout.returncode, 1)
            self.assertIn("forbidden amalgamation/module source", layout.stderr)
            self.assertIn("has no implementation record", layout.stderr)

    def test_one_public_operation_symbol_per_source_is_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            self.make_repository_fixture(temporary_root)
            inventory_path = (
                temporary_root
                / "library/generated/api_inventory/implementation_inventory.json"
            )
            inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
            item = inventory_record(inventory, "std.bytes::equal", "operation")
            item["item_kind"] = "operation"
            item["implementation"] = {
                "kind": "source",
                "source": "library/std/bytes/source/equal.c",
                "c_symbol": "r_std_bytes_equal",
            }
            inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
            source_directory = temporary_root / "library/std/bytes/source"
            source_directory.mkdir(exist_ok=True)
            source_path = source_directory / "equal.c"
            source_path.write_text(
                "int r_std_bytes_equal(void);\n"
                "int r_std_bytes_equal(void) {\n"
                "    return 0;\n"
                "}\n",
                encoding="utf-8",
            )
            cmake_path = temporary_root / "library/std/bytes/CMakeLists.txt"
            cmake_text = cmake_path.read_text(encoding="utf-8")
            cmake_path.write_text(
                cmake_text.replace(
                    "set(R_STD_BYTES_PUBLIC_SOURCES)",
                    "set(R_STD_BYTES_PUBLIC_SOURCES source/equal.c)",
                ),
                encoding="utf-8",
            )

            valid = run_tool(str(LAYOUT_CHECKER), "--root", str(temporary_root))
            self.assertEqual(valid.returncode, 0, valid.stderr)

            source_path.write_text(
                source_path.read_text(encoding="utf-8")
                + "int r_std_bytes_second(void);\n"
                + "int r_std_bytes_second(void) {\n"
                + "    return 0;\n"
                + "}\n",
                encoding="utf-8",
            )
            invalid = run_tool(str(LAYOUT_CHECKER), "--root", str(temporary_root))
            self.assertEqual(invalid.returncode, 1)
            self.assertIn("must define exactly public symbol", invalid.stderr)


if __name__ == "__main__":
    unittest.main()
