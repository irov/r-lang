#!/usr/bin/env python3
"""Guard against coverage counts inflated by strings, fixtures or empty families."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import check_example_coverage as coverage
import check_example_syntax as syntax


class CoverageTests(unittest.TestCase):
    def test_comments_and_literals_do_not_count(self):
        source='''// std.math::fake_comment
/* std.math::fake_block */
str text = "std.math::fake_string";
str formatted = f"std.math::fake_format {name}";
f64 answer = std.math::sqrt_f64(input);
'''
        clean=coverage.code_only(source)
        self.assertNotIn('fake',clean)
        self.assertIn('std.math::sqrt_f64',clean)

    def test_syntax_names_require_nodes_not_literal_text(self):
        cst = '(block\n  (token string_literal "(thread_scope_statement")\n  (if_statement\n  )\n)'
        self.assertEqual(syntax.node_names(cst), {'block', 'if_statement'})

    def test_empty_family_is_not_covered(self):
        family={'id':'std.math::missing_S','item_kind':'operation_schema'}
        with self.assertRaisesRegex(ValueError,'no concrete records'):
            coverage.spellings(family,[family])

    def test_resolved_types_exclude_registry_imports_and_literal_text(self):
        hir = '''(types (type 1 (standard "std.c::handle")))
(program
  (module "example.demo"
    (local type=(standard "std.sync::rw_read_guard" u32))
    (local type=(struct "std.iter"::"map_iter(i32,u32)"))
    (literal value="(standard \\\"std.c::c_string\\\")")
  )
  (module "std.library" (local type=(standard "std.thread::thread")))
)'''
        self.assertEqual(coverage.hir_types(hir, {'example.demo'}),
                         {'std.sync::rw_read_guard', 'std.iter::map_iter'})

    def test_resolved_operations_exclude_other_modules_and_literal_text(self):
        hir = '(program (module "example.demo" (standard_call operation=std.math::sqrt_f64)' \
              ' (call source_operation="std.string::as_str")' \
              ' (literal value="operation=std.math::fake"))' \
              ' (module "std.other" (standard_call operation=std.math::cos_f64)))'
        self.assertEqual(coverage.hir_types(hir, {'example.demo'}, operations=True),
                         {'std.math::sqrt_f64', 'std.string::as_str'})

    def test_every_concrete_family_member_is_required(self):
        family={'id':'std.math::sample_S','item_kind':'operation_schema'}
        members=[{'id':'std.math::sample_f32','item_kind':'operation'},
                 {'id':'std.math::sample_f64','item_kind':'operation'}]
        self.assertEqual(coverage.spellings(family,[family,*members]),
                         ['std.math::sample_f32','std.math::sample_f64'])

    def test_empty_pattern_and_test_sources_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve()
            (root/'tests').mkdir()
            (root/'tests/fixture.r').write_text('i32 main() { return 0; }')
            with patch.object(coverage,'ROOT',root):
                with self.assertRaisesRegex(ValueError,'empty source pattern'):
                    coverage.source_files({'id':'missing','sources':['examples/missing/*.r']})
                with self.assertRaisesRegex(ValueError,'non-example source'):
                    coverage.source_files({'id':'fixture','sources':['tests/*.r']})

    def test_stale_test_names_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve()
            (root/'examples/demo').mkdir(parents=True)
            (root/'examples/demo/README.md').write_text('# Demo\n')
            (root/'examples/demo/main.r').write_text('i32 main() { return 0; }')
            app={'id':'demo','kind':'application','sources':['examples/demo/main.r'],
                 'tests':['r_obsolete_test_name']}
            with patch.object(coverage,'ROOT',root):
                with self.assertRaisesRegex(ValueError,'unregistered tests'):
                    coverage.validate_catalogue({'applications':[app]}, {'r_actual_test_name'})


if __name__=='__main__': unittest.main()
