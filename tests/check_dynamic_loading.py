#!/usr/bin/env python3
"""Check the rejections of loadable libraries (Core R-FFI-0063, Library R-SLIB-DL-0004, B9.3): an
import of a loadable block is not called directly, the block declares no object, std.dl::resolve
takes an import of such a block, and the import is still verified against its headers."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

BLOCK = '''
import std.dl;
@link(name = "system.loader", kind = "loadable")
@header("stdlib.h")
extern "C" {
    @safety("LOADER-ABS", "none")
    c_int abs(c_int value);
    @safety("LOADER-LABS", "none")
    c_long labs(c_long value);
}
'''

# name -> (source after the block, [(line, column, code, rule, message)]); the block takes lines
# 2..10 after the module declaration and line 11 is empty.
REJECTED = {
    'direct-call': ('''
void direct() {
    unsafe { abs(1i32 as c_int) as void; }
}
''', [(13, 14, 'R-DIAG-FFI-003', 'R-FFI-0063',
       'a function of a loadable library is called through the pointer std.dl::resolve returns')]),
    'resolve-arguments': ('''
c_int local_function(c_int value) { return value; }
void wrong(std.dl::library system) {
    std.dl::resolve(&system, local_function) as void;
    std.dl::resolve(&system) as void;
}
''', [(14, 5, 'R-DIAG-FFI-003', 'R-FFI-0063',
       'the second argument of std.dl::resolve names an import of a loadable block'),
      (15, 5, 'R-DIAG-FFI-003', 'R-FFI-0063',
       'std.dl::resolve takes a library and an import of a loadable block')]),
    'resolve-type': ('''
void typed(std.dl::library system) {
    raw fn(c_int) -> c_int never_null = std.dl::resolve(&system, abs);
    never_null as void;
}
''', [(13, 41, 'R-DIAG-FFI-005', 'R-FFI-0025',
       'raw function pointers require an exact prototype and cannot discard nullability or add '
       'resource guarantees')]),
}

OBJECT = '''
@link(name = "system.loader", kind = "loadable")
@header("stdlib.h")
extern "C" {
    @safety("LOADER-OBJECT", "none")
    c_int loader_object;
}
'''

PROTOTYPE = BLOCK.replace('c_long labs(c_long value);', 'c_int labs(c_long value);') + '''
i32 main() {
    try {
        std.dl::library system = std.dl::open("libSystem.B.dylib");
        raw fn?(c_long) -> c_int wrong = std.dl::resolve(&system, labs);
        wrong as void;
    } catch (std.dl::dl_error failure) { failure as void; }
    return 0;
}
'''

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', required=True)
    parser.add_argument('--link-manifest', required=True)
    parser.add_argument('--target-manifest', required=True)
    parser.add_argument('--header-dir', required=True)
    args = parser.parse_args()
    sysroot = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], capture_output=True,
                             text=True, check=True).stdout.strip()
    with tempfile.TemporaryDirectory(prefix='r-dynamic-loading-') as directory:
        root = Path(directory)

        def run(name, text, emit='hir'):
            path = root / f'{name}.r'
            path.write_text(f'module loading.{name.replace("-", "_")};\n' + text.lstrip('\n'))
            return subprocess.run(
                [args.front, f'--emit={emit}', '--library-map', args.library_map,
                 '--link-manifest', args.link_manifest, '--target-manifest', args.target_manifest,
                 '--sysroot', sysroot, '--abi-header-dir', args.header_dir, str(path)],
                capture_output=True, text=True, timeout=120)

        def diagnostics(result):
            return sorted((int(m['line']), int(m['column']), m['code'], m['rule'], m['message'])
                          for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m)

        for name, (text, expected) in REJECTED.items():
            result = run(name, BLOCK.lstrip('\n') + text)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = diagnostics(result)
            assert len(found) == len(expected), (name, found)
            for diagnostic, wanted in zip(found, sorted(expected, key=lambda e: e[:2])):
                assert diagnostic[:4] == wanted[:4], (name, diagnostic, wanted)
                assert wanted[4] is None or diagnostic[4] == wanted[4], (name, diagnostic)

        result = run('object', OBJECT)
        assert result.returncode == 1, result.stderr
        assert diagnostics(result) == [(6, 11, 'R-DIAG-FFI-003', 'R-FFI-0063',
                                        'a loadable library provides functions only; an object '
                                        'of it has no address before the program opens it')], \
            result.stderr

        # The import is verified against <stdlib.h> like every import (R-FFI-0042).
        result = run('prototype', PROTOTYPE.lstrip('\n'), 'llvm-ir')
        assert result.returncode == 1, result.stderr
        assert any(d[:4] == (9, 11, 'R-DIAG-FFI-004', 'R-FFI-0021') for d in diagnostics(result)), \
            result.stderr
    print('dynamic loading checks passed')


if __name__ == '__main__':
    main()
