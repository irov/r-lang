#!/usr/bin/env python3
"""Check associated type constraints `P::Name: constraints` in generic function headers (Core
R-TYPE-0043, M19): diagnostics and their positions, calls whose arguments are dependent, the HIR
of a header with such an entry, and its interface record."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

PRELUDE = '''
trait Ranked { i32 rank(const Self* this); };
impl Ranked for i32 { i32 rank(const i32* this) { return *this; } };
struct Plain { i32 v; };
struct counter { i32 left; };
impl core::Iterator for counter {
    type Item = i32;
    o<i32> next(counter* this) {
        if (this->left == 0) { return o::none; }
        this->left -= 1;
        return o::some(this->left);
    }
};
struct plains { i32 left; };
impl core::Iterator for plains {
    type Item = Plain;
    o<Plain> next(plains* this) {
        if (this->left == 0) { return o::none; }
        this->left -= 1;
        return o::some(Plain {.v = this->left});
    }
};
@generic<I: core::Iterator, I::Item: Ranked & copy>
i32 top(I inner) {
    i32 best = 0;
    for (I::Item value in move inner) {
        if (value.rank() > best) { best = value.rank(); }
    }
    return best;
}
'''

# name -> (source after the prelude, [(line, column, code, rule, message fragment)]); lines count
# from the first line after the prelude.
REJECTED = {
    'unknown-parameter': ('''
@generic<T: copy, U::Item: copy>
T first(T value) { return value; }
''', [(1, 19, 'R-DIAG-NAME-001', 'R-TYPE-0043', 'shall name a parameter of its header')]),
    'undeclared': ('''
@generic<I: core::Iterator, I::Missing: copy>
usize none(I inner) { return 0usize; }
''', [(1, 32, 'R-DIAG-TRAIT-001', 'R-TYPE-0043',
       'no trait constraint of the parameter declares this associated type')]),
    'ambiguous': ('''
trait Source { type Item; };
@generic<I: core::Iterator & Source, I::Item: copy>
usize none(I inner) { return 0usize; }
''', [(2, 41, 'R-DIAG-TRAIT-001', 'R-TYPE-0043',
       'more than one trait constraint of the parameter declares this associated type')]),
    'callable': ('''
@generic<I: core::Iterator, I::Item: fn(i32) -> i32>
usize none(I inner) { return 0usize; }
''', [(1, 38, 'R-DIAG-TRAIT-001', 'R-TYPE-0043',
       'takes capability and nominal trait constraints')]),
    'struct-header': ('''
@generic<I: core::Iterator, I::Item: copy>
struct holder { I inner; };
''', [(1, 29, 'R-DIAG-TYPE-001', 'R-TYPE-0043',
       'belongs to the header of a generic function')]),
    'instantiation': ('''
i32 f() { return top(plains {.left = 2}); }
''', [(1, 21, 'R-DIAG-TYPE-001', 'R-TYPE-0043', 'does not implement the required trait')]),
    'dependent-call': ('''
@generic<I: core::Iterator, I::Item: copy>
i32 outer(I inner) { return top(move inner); }
''', [(2, 32, 'R-DIAG-TYPE-001', 'R-TYPE-0043', 'does not implement the required trait')]),
    'dependent-capability': ('''
@generic<I: core::Iterator>
i32 outer(I inner) { return top(move inner); }
''', [(2, 32, 'R-DIAG-TYPE-001', 'R-TYPE-0001', 'does not prove the required constraints')]),
    'body-without-constraint': ('''
@generic<I: core::Iterator, I::Item: copy>
i32 low(I inner) {
    for (I::Item value in move inner) { return value.rank(); }
    return 0;
}
''', [(3, 48, 'R-DIAG-NAME-001', 'R-TYPE-0035',
       'no trait constraint of this type parameter provides this method')]),
}

ACCEPTED = '''
trait Sub : core::Iterator {};
impl Sub for counter {};
@generic<I: Sub, I::Item: Ranked & copy>
i32 through(I inner) { return top(move inner); }
i32 f() { return top(counter {.left = 3}) + through(counter {.left = 5}); }
'''

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    prelude_lines = PRELUDE.strip('\n').count('\n') + 2
    with tempfile.TemporaryDirectory(prefix='r-projection-constraints-') as directory:
        root = Path(directory)

        def run(mode, text, name):
            path = root / f'{name}.r'
            path.write_text(f'module projections.{name.replace("-", "_")};\n' +
                            PRELUDE.strip('\n') + '\n' + text.lstrip('\n'))
            return subprocess.run([args.front, f'--emit={mode}', str(path)],
                                  capture_output=True, text=True, timeout=60)

        for name, (text, expected) in REJECTED.items():
            result = run('mir', text, name)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = [m.groupdict() for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m]
            assert len(found) == len(expected), (name, result.stderr)
            for diagnostic, (line, column, code, rule, fragment) in zip(found, expected):
                position = (int(diagnostic['line']) - prelude_lines, int(diagnostic['column']))
                assert position == (line, column), (name, position, diagnostic, result.stderr)
                assert diagnostic['code'] == code and diagnostic['rule'] == rule, (name, diagnostic)
                assert fragment in diagnostic['message'], (name, diagnostic)

        # M19-1: the HIR lists the holder of an entry's constraints under the associated name.
        result = run('hir', ACCEPTED, 'accepted')
        assert result.returncode == 0, result.stderr
        assert '(parameter "Item")' in result.stdout, result.stdout[:400]

        result = run('interface', ACCEPTED, 'accepted')
        assert result.returncode == 0, result.stderr
        text = result.stdout
        assert 'version=34' in text, text[:200]
        entry = ('associated_constraints=((associated="I"::"Item" constraints=(copy (name="Ranked" '
                 'module="projections.accepted" arguments=()))))')
        assert text.count(entry) == 2, (entry, text)
    print('projection constraint checks passed')


if __name__ == '__main__':
    main()
