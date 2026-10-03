#!/usr/bin/env python3
"""Regression tests for the closed direct runtime-entry stack inventory."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = REPOSITORY_ROOT / "tools/generate_runtime_entry_stack.py"
INVENTORY = (
    REPOSITORY_ROOT
    / "targets/arm64-apple-darwin.hosted-native-async.runtime-entry-stack.json"
)
COMPILER_BUILD = "clang-2100.3.34.2"
FRAME_CEILING_BYTES = 262144


def run_tool(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(GENERATOR), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def inventory_records(value: dict[str, Any]) -> list[dict[str, Any]]:
    return value["external_entries"] + value["header_static_inline_helpers"]


def stack_usage_lines(value: dict[str, Any]) -> list[str]:
    return [
        f"{record['source']}:{index + 1}:1:{record['c_symbol']}\t{index + 1}\tstatic"
        for index, record in enumerate(inventory_records(value))
    ]


def write_stack_usage(path: Path, lines: list[str]) -> None:
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


class RuntimeEntryStackTests(unittest.TestCase):
    def setUp(self) -> None:
        self.inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))

    def base_arguments(self, inventory: Path = INVENTORY) -> tuple[str, ...]:
        return (
            "--root",
            str(REPOSITORY_ROOT),
            "--inventory",
            str(inventory),
        )

    def header_arguments(
        self, report: Path, output: Path, mode: str = "--write-header"
    ) -> tuple[str, ...]:
        return (
            *self.base_arguments(),
            mode,
            "--output-header",
            str(output),
            "--compiler-build",
            COMPILER_BUILD,
            "--stack-usage",
            str(report),
        )

    def test_repository_inventory_is_closed_and_current(self) -> None:
        result = run_tool(*self.base_arguments(), "--validate-inventory")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.inventory["schema"], "r-runtime-entry-stack-inventory-0.1")
        self.assertFalse(self.inventory["conformance_claim"])
        self.assertEqual(len(self.inventory["external_entries"]), 871)
        self.assertEqual(len(self.inventory["header_static_inline_helpers"]), 135)
        self.assertEqual(
            self.inventory["coverage"]["scope"], "direct-project-entry-frame-only"
        )
        self.assertFalse(
            self.inventory["coverage"]["production_linking_uses_measured_objects"]
        )
        self.assertEqual(
            self.inventory["coverage"]["transitive_project_call_chain"], "not-measured"
        )
        self.assertEqual(
            self.inventory["coverage"]["native_system_library_frames"], "not-measured"
        )
        surfaces = self.inventory["closed_source_surfaces"]
        self.assertEqual(surfaces["generated_c_emitter"], "compiler/codegen/c17.c")
        self.assertEqual(
            surfaces["named_standard_move_registry"],
            "compiler/source/named_standard_move_abi.generated.inc",
        )

        records = inventory_records(self.inventory)
        self.assertEqual(len(records), len({record["c_symbol"] for record in records}))
        for record in records:
            self.assertEqual(
                set(record),
                {
                    "c_symbol",
                    "source",
                    "category",
                    "linkage",
                    "direct_frame_measurement_available",
                },
            )
            self.assertTrue(record["direct_frame_measurement_available"])

    def test_source_surfaces_reject_new_emitted_call_and_move_registry_helper(self) -> None:
        surface_paths = (
            "compiler/codegen/c17.c",
            *(str(path.relative_to(REPOSITORY_ROOT)) for path in sorted(
                (REPOSITORY_ROOT / "compiler/codegen").glob("*.inc")
            )),
            "compiler/source/named_standard_move_abi.generated.inc",
            "compiler/source/standard_async_sync.h",
            "compiler/source/standard_fs_async.h",
            "compiler/source/standard_scoped_operations.h",
            "compiler/source/standard_math_operations.generated.inc",
            "compiler/source/standard_sync.h",
            "library/std/async/include/r_std_async.h",
            "library/std/c/include/r_std_c.h",
            "library/std/format/include/r_std_format.h",
            "library/std/fs/include/r_std_fs.h",
            "library/std/io/include/r_std_io.h",
            "library/std/sync/include/r_std_sync.h",
            "library/std/string/include/r_std_string.h",
            "library/std/json/include/r_std_json.h",
            "library/std/json/include/r_std_json_reader.h",
            "library/std/net/include/r_std_net.h",
            "library/std/process/include/r_std_process.h",
            "library/std/secret/include/r_std_secret.h",
            "library/std/signal/include/r_std_signal.h",
            "library/std/thread/include/r_std_thread.h",
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            for relative_path in surface_paths:
                destination = temporary_root / relative_path
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPOSITORY_ROOT / relative_path, destination)

            generated_inventory = temporary_root / "entry-stack.json"
            baseline = run_tool(
                "--root",
                str(temporary_root),
                "--inventory",
                str(generated_inventory),
                "--write-inventory",
            )
            self.assertEqual(baseline.returncode, 0, baseline.stderr)

            emitter = temporary_root / "compiler/codegen/c17.c"
            emitter.write_text(
                emitter.read_text(encoding="utf-8")
                + '\nstatic const char *const r_test_prefix = "r_runtime_formatted_";\n',
                encoding="utf-8",
            )
            formatted_prefix = run_tool(
                "--root",
                str(temporary_root),
                "--inventory",
                str(generated_inventory),
                "--write-inventory",
            )
            self.assertEqual(formatted_prefix.returncode, 0, formatted_prefix.stderr)

            emitter.write_text(
                emitter.read_text(encoding="utf-8")
                + '\nstatic const char *const r_test_entry = "r_runtime_new_direct_entry(";\n',
                encoding="utf-8",
            )
            new_call = run_tool(
                "--root",
                str(temporary_root),
                "--inventory",
                str(generated_inventory),
                "--write-inventory",
            )
            self.assertEqual(new_call.returncode, 1)
            self.assertIn("generated project entry surface is not closed", new_call.stderr)
            self.assertIn("new=r_runtime_new_direct_entry", new_call.stderr)

            shutil.copy2(REPOSITORY_ROOT / "compiler/codegen/c17.c", emitter)
            registry = (
                temporary_root / "compiler/source/named_standard_move_abi.generated.inc"
            )
            registry.write_text(
                registry.read_text(encoding="utf-8")
                + '\nstatic const char *const r_test_helper = "r_std_fs_new_destroy";\n',
                encoding="utf-8",
            )
            new_helper = run_tool(
                "--root",
                str(temporary_root),
                "--inventory",
                str(generated_inventory),
                "--write-inventory",
            )
            self.assertEqual(new_helper.returncode, 1)
            self.assertIn("named move registry helper surface is not closed", new_helper.stderr)
            self.assertIn("new=r_std_fs_new_destroy", new_helper.stderr)

    def test_closed_inventory_rejects_missing_new_duplicate_and_wrong_source(self) -> None:
        mutations = []

        missing = json.loads(json.dumps(self.inventory))
        missing["external_entries"].pop()
        mutations.append(("missing", missing))

        new = json.loads(json.dumps(self.inventory))
        new["external_entries"].append(
            {
                "c_symbol": "r_runtime_uninventoried_entry",
                "source": "runtime/source/allocator.c",
                "category": "runtime-other",
                "linkage": "external",
                "direct_frame_measurement_available": True,
            }
        )
        mutations.append(("new", new))

        duplicate = json.loads(json.dumps(self.inventory))
        duplicate["external_entries"].append(duplicate["external_entries"][0])
        mutations.append(("duplicate", duplicate))

        wrong_source = json.loads(json.dumps(self.inventory))
        wrong_source["external_entries"][0]["source"] = "runtime/source/allocator.c"
        mutations.append(("wrong-source", wrong_source))

        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            for name, value in mutations:
                with self.subTest(mutation=name):
                    path = temporary_root / f"{name}.json"
                    write_json(path, value)
                    result = run_tool(*self.base_arguments(path), "--validate-inventory")
                    self.assertEqual(result.returncode, 1)
                    self.assertIn("inventory is not the closed catalog", result.stderr)

    def test_header_is_deterministic_and_verifiable(self) -> None:
        lines = stack_usage_lines(self.inventory)
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            first_report = temporary_root / "first.su"
            second_report = temporary_root / "second.su"
            first_header = temporary_root / "first.h"
            second_header = temporary_root / "second.h"
            write_stack_usage(first_report, lines)
            write_stack_usage(second_report, list(reversed(lines)))

            first = run_tool(*self.header_arguments(first_report, first_header))
            second = run_tool(*self.header_arguments(second_report, second_header))
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(first_header.read_bytes(), second_header.read_bytes())

            verification = run_tool(
                *self.header_arguments(first_report, first_header, "--verify-header")
            )
            self.assertEqual(verification.returncode, 0, verification.stderr)

            header = first_header.read_text(encoding="utf-8")
            self.assertIn("R_RUNTIME_ENTRY_STACK_CONFORMANCE_CLAIM 0", header)
            self.assertIn(
                f"R_RUNTIME_ENTRY_STACK_ENTRY_COUNT ((size_t){len(inventory_records(self.inventory))})",
                header,
            )
            self.assertIn(
                "R_RUNTIME_ENTRY_FRAME_r_runtime_stack_require ((size_t)", header
            )
            self.assertIn("transitive and native frames are not bounded", header)

            first_header.write_text(header + "/* stale */\n", encoding="utf-8")
            stale = run_tool(
                *self.header_arguments(first_report, first_header, "--verify-header")
            )
            self.assertEqual(stale.returncode, 1)
            self.assertIn("header is stale", stale.stderr)

    def test_header_requires_pinned_stack_usage_and_compiler_build(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            output = temporary_root / "output.h"
            missing = run_tool(
                *self.base_arguments(),
                "--verify-header",
                "--output-header",
                str(output),
                "--compiler-build",
                COMPILER_BUILD,
            )
            self.assertEqual(missing.returncode, 1)
            self.assertIn("at least one real .su stack-usage input", missing.stderr)

            report = temporary_root / "entries.su"
            write_stack_usage(report, stack_usage_lines(self.inventory))
            wrong_build = run_tool(
                *self.base_arguments(),
                "--write-header",
                "--output-header",
                str(output),
                "--compiler-build",
                "clang-2100.1.1.102",
                "--stack-usage",
                str(report),
            )
            self.assertEqual(wrong_build.returncode, 1)
            self.assertIn("requires compiler build", wrong_build.stderr)

    def test_stack_usage_rejects_missing_duplicate_nonstatic_wrong_source_and_ceiling(
        self,
    ) -> None:
        original = stack_usage_lines(self.inventory)
        first = inventory_records(self.inventory)[0]
        mutations = (
            ("missing", original[1:], "missing stack-usage records"),
            (
                "duplicate",
                [*original, original[0]],
                "duplicate stack-usage record",
            ),
            (
                "nonstatic",
                [original[0].removesuffix("static") + "dynamic", *original[1:]],
                "non-static stack frame",
            ),
            (
                "wrong-source",
                [
                    f"runtime/source/allocator.c:1:1:{first['c_symbol']}\t1\tstatic",
                    *original[1:],
                ],
                "wrong source",
            ),
            (
                "ceiling",
                [
                    f"{first['source']}:1:1:{first['c_symbol']}\t"
                    f"{FRAME_CEILING_BYTES + 1}\tstatic",
                    *original[1:],
                ],
                "direct entry frame exceeds ceiling",
            ),
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            for name, lines, expected_error in mutations:
                with self.subTest(mutation=name):
                    report = temporary_root / f"{name}.su"
                    output = temporary_root / f"{name}.h"
                    write_stack_usage(report, lines)
                    result = run_tool(*self.header_arguments(report, output))
                    self.assertEqual(result.returncode, 1)
                    self.assertIn(expected_error, result.stderr)

    def test_stack_usage_enforces_report_and_record_caps(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            output = temporary_root / "output.h"
            reports = []
            for index in range(129):
                report = temporary_root / f"report-{index}.su"
                write_stack_usage(
                    report,
                    [f"runtime/source/allocator.c:1:1:ignored_{index}\t1\tstatic"],
                )
                reports.extend(("--stack-usage", str(report)))
            too_many_reports = run_tool(
                *self.base_arguments(),
                "--write-header",
                "--output-header",
                str(output),
                "--compiler-build",
                COMPILER_BUILD,
                *reports,
            )
            self.assertEqual(too_many_reports.returncode, 1)
            self.assertIn("input report cap exceeded", too_many_reports.stderr)

            record_report = temporary_root / "records.su"
            lines = stack_usage_lines(self.inventory)
            lines.extend(
                f"runtime/source/allocator.c:{index + 1}:1:ignored_{index}\t1\tstatic"
                for index in range(4097)
            )
            write_stack_usage(record_report, lines)
            too_many_records = run_tool(*self.header_arguments(record_report, output))
            self.assertEqual(too_many_records.returncode, 1)
            self.assertIn("input record cap exceeded", too_many_records.stderr)


if __name__ == "__main__":
    unittest.main()
