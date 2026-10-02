#!/usr/bin/env python3
"""Check derived implementations (Core R-AGG-0012): diagnostics at the capability name, one per
derivation, the implicit std.cmp import, dumps without the generated declarations and the
interface records of the derived implementations."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

# name -> (source, [(line, column, code, rule, message fragment)], exact list)
REJECTED = {
    'unknown-and-duplicate': ('''
@derive(copy, equal, equal)
struct P { i32 x; };
@derive()
struct Q { i32 x; };
''', [(2, 9, 'R-DIAG-SYN-002', 'R-AGG-0012', 'lists only clone, equal, ordered, key and format'),
      (2, 22, 'R-DIAG-SYN-002', 'R-AGG-0012', 'each capability at most once'),
      (4, 1, 'R-DIAG-SYN-002', 'R-AGG-0012', 'at least one capability')]),
    'struct-field': ('''
struct Inner { i32 v; };
@derive(equal, ordered, key)
struct Outer { i32 a; Inner inner; };
''', [(3, 9, 'R-DIAG-TRAIT-001', 'R-AGG-0012', '`inner` does not'),
      (3, 16, 'R-DIAG-TRAIT-001', 'R-AGG-0012', 'implement std.cmp::Ordered; `inner` does not'),
      (3, 25, 'R-DIAG-TRAIT-001', 'R-AGG-0012', 'prove key (R-FUNC-0009); `inner` does not')]),
    'variant-payload': ('''
struct Plain { i32 v; };
@derive(equal)
enum E { a(i32), b { Plain first; i32 second; }, c };
''', [(3, 9, 'R-DIAG-TRAIT-001', 'R-AGG-0012', '`b.first` does not')]),
    'variant-single': ('''
struct Plain { i32 v; };
@derive(key)
enum E { a(Plain), c };
''', [(3, 9, 'R-DIAG-TRAIT-001', 'R-AGG-0012', '`a` does not')]),
    'clone-borrowed-field': ('''
@derive(clone)
struct Named { std.string::string name; str alias; };
''', [(2, 9, 'R-DIAG-TRAIT-001', 'R-AGG-0012', '`alias` does not')]),
    'explicit-impl': ('''
import std.cmp;
@derive(equal)
struct P { i32 x; };
impl std.cmp::Equal for P {
    bool eq(const P* this, const P* other) { return this->x == other->x; }
};
''', [(3, 9, 'R-DIAG-TRAIT-001', 'R-TYPE-0042', 'duplicate implementation')]),
    'explicit-key-hooks': ('''
@derive(key)
struct P { i32 x; };
u64 P::hash(const P* value) { return core::hash(&value->x); }
bool P::equal(const P* left, const P* right) { return core::key_equal(&left->x, &right->x); }
''', [(2, 9, 'R-DIAG-NAME-002', None, 'duplicate')]),
    'explicit-clone-hook': ('''
@derive(clone)
struct P { std.string::string x; };
P P::clone(const P* value) throws std.alloc::alloc_error {
    return P { .x = core::clone(&value->x) };
}
''', [(2, 9, 'R-DIAG-NAME-002', 'R-AGG-0012', 'clone hook does not also derive clone')]),
    'family-base': ('''
@derive(equal, key)
error storage_error { i32 code; };
error disk_error : storage_error { u32 sector; };
''', [(2, 9, 'R-DIAG-TYPE-001', 'R-AGG-0012', 'holds any member of its family')]),
    'static-condition': ('''
@if (!(core::profile is freestanding)) {
    @derive(equal)
    struct P { i32 x; };
}
''', [(3, 13, 'R-DIAG-SYN-002', 'R-AGG-0012', 'module-scope declaration outside static')]),
    'function': ('''
@derive(equal)
i32 f() { return 1; }
''', [(2, 1, 'R-DIAG-SYN-002', 'R-GRAM-0007', 'function attribute')]),
    'c-aggregate': ('''
extern "C" {
    @derive(equal) @repr(C) @c_type(name = "point", kind = "struct") struct point { i32 x; };
}
''', [(3, 5, 'R-DIAG-SYN-002', 'R-AGG-0012', 'not available for a C aggregate')], False),
    'option-without-trait': ('''
import std.cmp;
struct Plain { i32 v; };
@generic<T: std.cmp::Equal>
bool same(const T* left, const T* right) { return left->eq(right); }
bool f() { o<Plain> a = o::none; return same(&a, &a); }
''', [(6, 45, 'R-DIAG-TYPE-001', 'R-TYPE-0043', 'does not implement the required trait')]),
    'format-member': ('''
struct Plain { i32 v; };
@derive(format)
struct Outer { i32 a; Plain inner; };
@derive(format)
enum E { a(i32), b { Plain first; }, c };
''', [(5, 9, 'R-DIAG-TRAIT-001', 'R-AGG-0012', 'satisfy core::Format; `b.first` does not'),
      (3, 9, 'R-DIAG-TRAIT-001', 'R-AGG-0012', 'satisfy core::Format; `inner` does not')]),
    'format-explicit-impl': ('''
@derive(format)
struct P { i32 x; };
impl core::Format for P {
    void format(const P* this, std.format::builder* out) throws std.alloc::alloc_error { }
};
''', [(2, 9, 'R-DIAG-TRAIT-001', 'R-TYPE-0042', 'duplicate implementation')]),
    'format-static-condition': ('''
@if (!(core::profile is freestanding)) {
    @derive(format)
    struct P { i32 x; };
}
''', [(3, 13, 'R-DIAG-SYN-002', 'R-AGG-0012', 'module-scope declaration outside static')]),
    'conditional-hook': ('''
@generic<T>
struct Box { T first; };
@generic<T: key>
u64 Box<T>::hash(const Box<T>* value) { return core::hash(&value->first); }
@generic<T: key>
bool Box<T>::equal(const Box<T>* left, const Box<T>* right) {
    return core::key_equal(&left->first, &right->first);
}
struct Plain { i32 v; };
bool f() { Box<Plain> a = {.first = Plain {.v = 1}}; return core::key_equal(&a, &a); }
''', [(11, 61, 'R-DIAG-TYPE-001', 'R-FUNC-0009', 'key contract is not available')]),
}

ACCEPTED = '''
@generic<T: key & copy>
@derive(equal, ordered, key, format)
struct Tagged { T value; u32 tag; };
@derive(clone, equal, ordered, key, format)
enum Shape { dot(i32), rect { i32 w; i32 h; }, empty };
i32 main() {
    Tagged<u32> a = {.value = 1u32, .tag = 2u32};
    Shape s = Shape::rect {.w = 1, .h = 2};
    if (a.eq(&a) == false || s.cmp(&s) != std.cmp::ordering::equal) { return 1; }
    return 0;
}
'''

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-derive-') as directory:
        root = Path(directory)

        def run(mode, text, name):
            path = root / f'{name}.r'
            path.write_text(f'module derive.{name.replace("-", "_")};\n' + text.lstrip('\n'))
            return subprocess.run(
                [args.front, f'--emit={mode}', '--library-map', args.library_map, str(path)],
                capture_output=True, text=True, timeout=60)

        for name, case in REJECTED.items():
            text, expected = case[0], case[1]
            exact = case[2] if len(case) > 2 else True
            result = run('hir', text, name)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = [m.groupdict() for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m]
            if not exact:
                # Other requirements of the declaration report their own diagnostics as well.
                found = [d for d in found if d['rule'] == 'R-AGG-0012']
            assert len(found) == len(expected), (name, result.stderr)
            for diagnostic, (line, column, code, rule, fragment) in zip(found, expected):
                assert (int(diagnostic['line']), int(diagnostic['column'])) == (line, column), (
                    name, diagnostic, result.stderr)
                assert diagnostic['code'] == code, (name, diagnostic)
                assert rule is None or diagnostic['rule'] == rule, (name, diagnostic)
                assert fragment in diagnostic['message'], (name, diagnostic)

        result = run('hir', ACCEPTED, 'accepted')
        assert result.returncode == 0, result.stderr
        # The dumps show the authored declarations only; the generated ones follow them.
        for mode in ('cst', 'ast'):
            result = run(mode, ACCEPTED, 'accepted')
            assert result.returncode == 0, result.stderr
            unit = result.stdout.split('\n; source ', 1)[0]
            assert 'impl_declaration' not in unit and 'switch_statement' not in unit, unit
            assert unit.count('struct_declaration') == 1 and unit.count('enum_declaration') == 1
        # The interface records the derived implementations and hooks as declared ones.
        result = run('interface', ACCEPTED, 'accepted')
        assert result.returncode == 0, result.stderr
        text = result.stdout
        for target in ('(apply "derive.accepted"::"Tagged" (parameter "T"))',
                       '(enum "derive.accepted"::"Shape")'):
            for trait in ('Equal', 'Ordered'):
                assert (f'(impl trait=(name="{trait}" module="std.cmp" arguments=()) '
                        f'target={target}') in text, (trait, target)
        assert 'name="derive.accepted::Tagged" kind=struct layout=declaration-order copy=true ' \
            'error=false derived=(equal ordered key format) ' in text, text
        assert 'name="derive.accepted::Shape" kind=enum layout=tagged-union copy=true ' \
            'error=false derived=(clone equal ordered key format) ' in text, text
        assert 'hash=(name="derive.accepted::Shape::hash"' in text, text
    print('derive checks passed')


if __name__ == '__main__':
    main()
