#!/usr/bin/env python3
"""Check function types and values (Core R-TYPE-0054): diagnostics and their positions, the
readable name of a call through a function value in recursion and resource chains, the HIR of a
conversion and of a dispatcher, and the interface records of function types."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

# name -> (source, [(line, column, code, rule, message fragment)])
REJECTED = {
    'mismatch': ('''
i64 wide(i32 v) { return 1i64; }
async i32 later(i32 v) { return v; }
void f() {
    fn(i32) -> i32 a = wide;
    fn(i32) -> i32 b = later;
}
''', [(5, 24, 'R-DIAG-TYPE-001', 'R-TYPE-0054', 'does not match the function type'),
      (6, 24, 'R-DIAG-TYPE-001', 'R-TYPE-0054', 'does not match the function type')]),
    'guarantee': ('''
i32 slow(i32 v) { return v; }
void f() { fn @noalloc (i32) -> i32 g = slow; }
''', [(3, 41, 'R-DIAG-TYPE-001', 'R-TYPE-0054', 'proves every resource guarantee')]),
    'lambda': ('''
void f() {
    i32 c = 3;
    fn i32 add(i32 v) move(c) { return v + c; }
    fn(i32) -> i32 g = add;
}
''', [(5, 24, 'R-DIAG-TYPE-001', 'R-TYPE-0054', 'a lambda does not convert')]),
    'mode': ('''
i32 a(i32 v) { return v; }
void f() { fn once(i32) -> i32 g = a; }
''', [(3, 12, 'R-DIAG-TYPE-001', 'R-TYPE-0054', 'no callable mode')]),
    'async-borrow': ('''
void f() { o<async fn(const i32*) -> i32> g = o::none; }
''', [(2, 14, 'R-DIAG-ASYNC-001', 'R-TYPE-0054', 'Send, unborrowed')]),
    # M42-2: a type that depends on a generic parameter is checked where a function converts to
    # the type of an instance, also inside the instance of a generic function.
    'async-instance': ('''
@generic<T>
struct holder { async fn(T) -> i32 step; };
@scoped
async i32 peek(const i32* v) { return *v; }
holder<const i32*> make() { return holder<const i32*> {.step = peek}; }
''', [(6, 64, 'R-DIAG-ASYNC-001', 'R-TYPE-0054', 'Send, unborrowed')]),
    'async-generic-body': ('''
@generic<T: send>
struct holder { async fn(T) -> i32 step; };
@generic<T: send>
@scoped
async i32 peek(T v) {
    (move v) as void;
    return 1;
}
@generic<T: send>
holder<T> wrap() { return holder<T> {.step = peek::<T>}; }
holder<const i32*> make() { return wrap::<const i32*>(); }
''', [(11, 46, 'R-DIAG-ASYNC-001', 'R-TYPE-0054', 'Send, unborrowed')]),
    'method': ('''
i32 a(i32 v) { return v; }
i32 f() { fn(i32) -> i32 x = a; return x.apply(1); }
''', [(3, 40, 'R-DIAG-NAME-001', 'R-TYPE-0054', 'provides only the method call')]),
    'recursion': ('''
i32 down(i32 n) {
    if (n == 0) { return 0; }
    fn(i32) -> i32 next = down;
    return next(n - 1);
}
''', [(4, 27, 'R-DIAG-STACK-001', 'R-FUNC-0004',
       'recursive call chain: down -> call through `fn(i32) -> i32` -> down')]),
    'resource': ('''
i32 a(i32 v) { return v; }
@noalloc
i32 run(fn(i32) -> i32 g) { return g(1); }
i32 f() { return run(a); }
''', [(4, 36, 'R-DIAG-RESOURCE-001', 'R-FUNC-0019',
       'run -> call through `fn(i32) -> i32` -> the function type does not promise @noalloc')]),
}

ACCEPTED = '''
error Rejected { i32 code; };
i32 half(i32 v) throws Rejected { return v / 2; }
@noalloc
i32 inc(i32 v) { return v + 1; }
async i32 later(i32 v) { return v; }
struct Route {
    fn(i32) -> i32 throws(Rejected) run;
    fn @noalloc (i32) -> i32 fast;
    async fn(i32) -> i32 slow;
};
Route make() { return Route {.run = half, .fast = inc, .slow = later}; }
i32 call(const Route* route) throws Rejected { return route->run(4) + route->fast(1); }
'''

# M42-1: async function types over generic parameters, checked in each instance.
GENERIC_ACCEPTED = '''
@generic<T: send & unborrowed>
struct holder { async fn(T) -> i32 step; };
@generic<T: send & unborrowed>
async i32 count(T v) {
    (move v) as void;
    return 1;
}
@generic<T: send & unborrowed>
holder<T> wrap() { return holder<T> {.step = count::<T>}; }
async i32 run() throws std.error::fault {
    holder<i64> h = wrap::<i64>();
    auto chosen = h.step;
    return await chosen(5i64);
}
'''

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-function-values-') as directory:
        root = Path(directory)

        def run(mode, text, name):
            path = root / f'{name}.r'
            path.write_text(f'module values.{name.replace("-", "_")};\n' + text.lstrip('\n'))
            return subprocess.run([args.front, f'--emit={mode}', str(path)],
                                  capture_output=True, text=True, timeout=60)

        for name, (text, expected) in REJECTED.items():
            result = run('mir', text, name)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = [m.groupdict() for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m]
            assert len(found) == len(expected), (name, result.stderr)
            for diagnostic, (line, column, code, rule, fragment) in zip(found, expected):
                assert (int(diagnostic['line']), int(diagnostic['column'])) == (line, column), (
                    name, diagnostic, result.stderr)
                assert diagnostic['code'] == code and diagnostic['rule'] == rule, (name, diagnostic)
                assert fragment in diagnostic['message'], (name, diagnostic)

        result = run('mir', GENERIC_ACCEPTED, 'generic')
        assert result.returncode == 0, result.stderr

        result = run('hir', ACCEPTED, 'accepted')
        assert result.returncode == 0, result.stderr
        hir = result.stdout
        # A conversion names its function and the function type; a call binds to the dispatcher,
        # whose declaration lists the value first and the arguments after it.
        assert re.search(r'\(function_address symbol=\d+ name="half" type=\(fn parameters=\(i32\) '
                         r'return=i32 throws=\(effects \(struct "values.accepted"::"Rejected"\)\)\)',
                         hir), hir
        assert 'name="inc" type=(fn parameters=(i32) return=i32 noalloc=true)' in hir, hir
        assert 'name="later" type=(fn parameters=(i32) return=i32 async=true)' in hir, hir
        assert re.search(r'\(parameter symbol=\d+ name="\$this" type=\(fn parameters=\(i32\)', hir), hir
        assert re.search(r'\(parameter symbol=\d+ name="\$argument1" type=i32\)', hir), hir

        result = run('interface', ACCEPTED, 'accepted')
        assert result.returncode == 0, result.stderr
        text = result.stdout
        assert '(interface version=32 ' in text or 'version=32' in text, text[:200]
        for field in ('(index=0 name="run" type=(fn parameters=(i32) return=i32 throws=(effects '
                      '(struct "values.accepted"::"Rejected")))',
                      '(index=1 name="fast" type=(fn parameters=(i32) return=i32 noalloc=true)',
                      '(index=2 name="slow" type=(fn parameters=(i32) return=i32 async=true)'):
            assert field in text, (field, text)
    print('function value checks passed')


if __name__ == '__main__':
    main()
