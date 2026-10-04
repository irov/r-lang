#!/usr/bin/env python3
"""Regression tests for the stack entry bounds of generated C17 (Core R-FUNC-0004, R-FUNC-0026)."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPOSITORY_ROOT / "tools/compute_stack_entries.py"


def run_tool(source: str, frames: dict[str, int], budget: int = 1 << 20) -> tuple[int, str, str]:
    """Runs the tool on one translation unit and its .su report; returns status, output, header."""

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source_path = root / "program.c"
        report_path = root / "program.su"
        output_path = root / "program.entries"
        source_path.write_text(source, encoding="utf-8")
        report_path.write_text(
            "".join(f"program.c:1:1:{name}\t{size}\tstatic\n" for name, size in frames.items()),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--source",
                str(source_path),
                "--report",
                str(report_path),
                "--output",
                str(output_path),
                "--budget",
                str(budget),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        header = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
        return result.returncode, result.stdout + result.stderr, header


ENTRY = """
static void entry(void) {
    size_t bound = R_STACK_ENTRY(entry);
    top();
}
"""


class ComputeStackEntriesTests(unittest.TestCase):
    def test_acyclic_chain_adds_the_longest_callee_path(self) -> None:
        source = """
static int leaf(int value) { return value; }
static int top(void) { return leaf(1) + leaf(2); }
""" + ENTRY
        status, output, header = run_tool(source, {"leaf": 16, "top": 32, "entry": 48})
        self.assertEqual(status, 0, output)
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)96)", header)

    def test_self_recursion_counts_its_depth(self) -> None:
        source = """
static int leaf(int value) { return value; }
static int top(int value) {
    /* R_STACK_RECURSION: 10 */
    return value == 0 ? leaf(0) : top(value - 1);
}
""" + ENTRY
        status, output, header = run_tool(source, {"leaf": 64, "top": 32, "entry": 16})
        self.assertEqual(status, 0, output)
        # Ten activations of top, then the deeper of the refused frame and leaf.
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)400)", header)
        self.assertIn("recursion top*10: 384 bytes", output)
        self.assertIn("#define R_STACK_RECURSION_top ((size_t)384) /* top*10 */", header)

    def test_mutual_recursion_counts_every_member(self) -> None:
        source = """
static int sum(int value);
static int product(int value) {
    /* R_STACK_RECURSION: 4 */
    return value == 0 ? 0 : sum(value - 1);
}
static int sum(int value) {
    /* R_STACK_RECURSION: 3 */
    return value == 0 ? 0 : product(value - 1);
}
static int top(void) { return sum(5); }
""" + ENTRY
        status, output, header = run_tool(source, {"product": 40, "sum": 24, "top": 8, "entry": 8})
        self.assertEqual(status, 0, output)
        # 4 * 40 + 3 * 24 = 232, plus the largest refused frame 40.
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)288)", header)
        self.assertIn("recursion product*4 sum*3: 272 bytes", output)
        self.assertIn(
            "#define R_STACK_RECURSION_product ((size_t)272) /* product*4 sum*3 */", header
        )

    def test_inlined_member_keeps_the_cycle(self) -> None:
        # sum was inlined into product: no frame of its own, yet product still recurses through it.
        source = """
static int sum(int value);
static int product(int value) {
    /* R_STACK_RECURSION: 4 */
    return value == 0 ? 0 : sum(value - 1);
}
static int sum(int value) {
    /* R_STACK_RECURSION: 3 */
    return value == 0 ? 0 : product(value - 1);
}
static int top(void) { return sum(5); }
""" + ENTRY
        status, output, header = run_tool(source, {"product": 40, "top": 8, "entry": 8})
        self.assertEqual(status, 0, output)
        # 4 * 40 + 3 * 0 = 160, plus the largest refused frame 40.
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)216)", header)

    def test_inlined_callee_still_counts_its_callees(self) -> None:
        source = """
static int leaf(int value) { return value; }
static int middle(int value) { return leaf(value); }
static int top(void) { return middle(1); }
""" + ENTRY
        status, output, header = run_tool(source, {"leaf": 64, "top": 8, "entry": 8})
        self.assertEqual(status, 0, output)
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)80)", header)

    def test_unmarked_cycle_fails(self) -> None:
        source = """
static int even(int value);
static int odd(int value) {
    /* R_STACK_RECURSION: 4 */
    return value == 0 ? 0 : even(value - 1);
}
static int even(int value) { return value == 0 ? 1 : odd(value - 1); }
static int top(void) { return even(5); }
""" + ENTRY
        status, output, _ = run_tool(source, {"odd": 16, "even": 16, "top": 8, "entry": 8})
        self.assertNotEqual(status, 0)
        self.assertIn("generated call graph is not acyclic: even -> odd -> even", output)

    def test_unmarked_self_call_is_not_a_cycle_edge(self) -> None:
        # Without a mark the name of the function inside its own body is not a call edge.
        source = """
static int top(void) { return (int)sizeof(&top); }
""" + ENTRY
        status, output, header = run_tool(source, {"top": 8, "entry": 8})
        self.assertEqual(status, 0, output)
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)16)", header)

    def test_recursion_above_the_budget_fails(self) -> None:
        source = """
static int top(int value) {
    /* R_STACK_RECURSION: 1000 */
    return value == 0 ? 0 : top(value - 1);
}
""" + ENTRY
        status, output, _ = run_tool(source, {"top": 128, "entry": 8}, budget=65536)
        self.assertNotEqual(status, 0)
        self.assertIn("entry entry needs 128136 bytes of stack", output)

    def test_unreachable_cycle_is_ignored(self) -> None:
        source = """
static int even(int value);
static int odd(int value) { return value == 0 ? 0 : even(value - 1); }
static int even(int value) { return value == 0 ? 1 : odd(value - 1); }
static int top(void) { return 1; }
""" + ENTRY
        status, output, header = run_tool(source, {"odd": 16, "even": 16, "top": 8, "entry": 8})
        self.assertEqual(status, 0, output)
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)16)", header)

    def test_guarded_call_starts_its_own_entry(self) -> None:
        # P4.4: a direct async call checks the bound of its body at run time and otherwise
        # starts a task, so it does not nest below the step that makes it.
        source = """
static int leaf(int value) { return value; }
static int direct(int value) { return leaf(value); }
static int top(void) {
    if (check(R_STACK_ENTRY(direct))) {
        /* R_STACK_GUARDED: direct */
        return direct(1);
    }
    return 0;
}
""" + ENTRY
        frames = {"leaf": 4096, "direct": 64, "top": 32, "entry": 16}
        status, output, header = run_tool(source, frames)
        self.assertEqual(status, 0, output)
        self.assertIn("#define R_STACK_ENTRY_entry ((size_t)48)", header)
        self.assertIn("#define R_STACK_ENTRY_direct ((size_t)4160)", header)

    def test_guarded_entry_above_the_budget_is_kept(self) -> None:
        source = """
static int direct(int value) { return value; }
static int top(void) {
    if (check(R_STACK_ENTRY(direct))) {
        /* R_STACK_GUARDED: direct */
        return direct(1);
    }
    return 0;
}
""" + ENTRY
        status, output, header = run_tool(source, {"direct": 8192, "top": 8, "entry": 8}, 4096)
        self.assertEqual(status, 0, output)
        self.assertIn("#define R_STACK_ENTRY_direct ((size_t)8192)", header)

    def test_guarded_call_without_an_entry_fails(self) -> None:
        source = """
static int direct(int value) { return value; }
static int top(void) {
    /* R_STACK_GUARDED: direct */
    return direct(1);
}
""" + ENTRY
        status, output, _ = run_tool(source, {"direct": 8, "top": 8, "entry": 8})
        self.assertNotEqual(status, 0)
        self.assertIn("R_STACK_GUARDED names a function without an entry: direct", output)


if __name__ == "__main__":
    unittest.main()
