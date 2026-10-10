#!/usr/bin/env python3
"""Check the rejections of C layouts (Core R-AGG-0014, R-FFI-0061, R-FFI-0062, B9.3): unions,
packed and aligned aggregates and bit-fields, each diagnostic at its exact position with its code,
rule and message."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

NO_ADDRESS = 'a packed field below the alignment of its type has no address to borrow'
REPRESENTATION = ('aggregate representation must be specified once as @repr(C), '
                  '@repr(C, packed) or @repr(C, align = N)')
ALIGN = '@align(N) of a field of a @repr(C) struct or union takes a power of two from 1 to 4096, once'
WIDTH = 'a bit-field width is an integer literal from one to the width of its type'
C_FORM = 'a packed struct or one with a bit-field is not generic and takes no @derive'
RANGE = 'a constant bit-field value is outside the range of the field'

# name -> (source, [(line, column, code, rule, message)]); line 1 is the module declaration.
REJECTED = {
    'union-declaration': ('''
union NoRepr { c_int a; };
@repr(C)
@generic<T>
union Generic { T a; };
@repr(C)
@derive(equal)
union Derived { c_int a; };
@repr(C)
union Initialized { c_int a = 1i32 as c_int; };
''', [(2, 7, 'R-DIAG-FFI-003', 'R-AGG-0014', 'a union requires @repr(C)'),
      (5, 7, 'R-DIAG-FFI-003', 'R-AGG-0014', 'a union cannot be generic'),
      (8, 7, 'R-DIAG-FFI-003', 'R-AGG-0014', 'a union takes no @derive'),
      (10, 21, 'R-DIAG-FFI-003', 'R-AGG-0014', 'a union field takes no initializer')]),
    'union-access': ('''
@repr(C)
union U { c_int a; c_double b; };
c_int read(U u) { return u.a; }
void write(U* u) { u->b = 2.0f64 as c_double; }
U two() { return U { .a = 1i32 as c_int, .b = 2.0f64 as c_double }; }
U based(U other) { return U { .a = 1i32 as c_int, ...other }; }
c_int allowed(U u) { unsafe { return u.a; } }
U zero() { return U {}; }
''', [(4, 28, 'R-DIAG-UNSAFE-001', 'R-AGG-0014',
       'access to a union field requires an unsafe context'),
      (5, 23, 'R-DIAG-UNSAFE-001', 'R-AGG-0014',
       'access to a union field requires an unsafe context'),
      (6, 20, 'R-DIAG-INIT-002', 'R-AGG-0014',
       'a union initializer sets one field at most and takes no base'),
      (7, 29, 'R-DIAG-INIT-002', 'R-AGG-0014',
       'a union initializer sets one field at most and takes no base')]),
    'union-reflection': ('''
@repr(C)
union U { c_int a; c_double b; };
void walk(U* u) { core::field(u, 0usize) as void; }
''', [(4, 19, 'R-DIAG-FFI-003', 'R-REFL-0006',
       'core::field cannot borrow the fields of a union, a packed field or a bit-field')]),
    'representation': ('''
@repr(C, packed, packed)
struct Twice { c_int a; };
@repr(C, align = 3)
struct Odd { c_int a; };
@repr(C, align = 8192)
struct Huge { c_int a; };
@repr(C, packed)
enum Packed { one, two };
@repr(C)
struct FieldOdd { @align(3) c_int a; };
struct Plain { @align(8) c_int a; };
''', [(2, 1, 'R-DIAG-FFI-003', 'R-FFI-0004', REPRESENTATION),
      (4, 1, 'R-DIAG-FFI-003', 'R-FFI-0004', REPRESENTATION),
      (6, 1, 'R-DIAG-FFI-003', 'R-FFI-0004', REPRESENTATION),
      (9, 6, 'R-DIAG-FFI-003', 'R-FFI-0061', 'packed and align apply to a struct or union'),
      (11, 19, 'R-DIAG-FFI-003', 'R-FFI-0061', ALIGN),
      (12, 16, 'R-DIAG-FFI-003', 'R-FFI-0061', ALIGN)]),
    'bit-field-declaration': ('''
@repr(C)
struct B {
    c_float f : 2;
    c_uint wide : 33;
    c_bool flag : 2;
    c_uint zero : 0;
    @align(4) c_uint aligned : 3;
    c_uint initialized : 3 = 1u32 as c_uint;
};
struct Plain { c_uint a : 3; };
@repr(C)
union InUnion { c_uint a : 3; c_int b; };
@repr(C)
@derive(equal)
struct Derived { c_uint a : 3; };
@repr(C, packed)
@derive(equal)
struct PackedDerived { c_int a; };
''', [(4, 5, 'R-DIAG-FFI-003', 'R-FFI-0062', 'a bit-field has a C ABI integer type or c_bool'),
      (5, 5, 'R-DIAG-FFI-003', 'R-FFI-0062', WIDTH),
      (6, 5, 'R-DIAG-FFI-003', 'R-FFI-0062', WIDTH),
      (7, 5, 'R-DIAG-FFI-003', 'R-FFI-0062', WIDTH),
      (8, 22, 'R-DIAG-FFI-003', 'R-FFI-0062', 'a bit-field takes no @align'),
      (9, 5, 'R-DIAG-FFI-003', 'R-FFI-0062', 'a bit-field takes no initializer'),
      (11, 16, 'R-DIAG-FFI-003', 'R-FFI-0062', 'a bit-field belongs to a @repr(C) struct'),
      (13, 17, 'R-DIAG-FFI-003', 'R-FFI-0062', 'a bit-field belongs to a @repr(C) struct'),
      (16, 8, 'R-DIAG-FFI-003', 'R-FFI-0062', C_FORM),
      (19, 8, 'R-DIAG-FFI-003', 'R-FFI-0061', C_FORM)]),
    'no-address': ('''
@repr(C, packed)
struct P { c_uchar a; c_int b; c_int[2] pair; };
@repr(C)
struct G { c_uint a : 3; c_uint b; };
@repr(C, packed)
struct H { c_uchar a; G inner; };
@repr(C, packed)
struct Aligned { c_uchar a; @align(4) c_int b; };
void use(const c_int* value) { value as void; }
void use_unsigned(const c_uint* value) { value as void; }
void packed_field(P p) { use(&p.b); }
void bit_field(G g) { use_unsigned(&g.a); }
void nested(H h) { use_unsigned(&h.inner.b); }
void element(P p) { use(&p.pair[1]); }
void slice(P p) { const c_int[] view = p.pair[0..2]; view as void; }
void raised(Aligned a) { use(&a.b); }
c_uint by_value(G g) { return g.a; }
''', [(12, 30, 'R-DIAG-FFI-003', 'R-FFI-0061', NO_ADDRESS),
      (13, 36, 'R-DIAG-FFI-003', 'R-FFI-0062', 'a bit-field has no address to borrow'),
      (14, 33, 'R-DIAG-FFI-003', 'R-FFI-0061', NO_ADDRESS),
      (15, 25, 'R-DIAG-FFI-003', 'R-FFI-0061', NO_ADDRESS),
      (16, 40, 'R-DIAG-FFI-003', 'R-FFI-0061', NO_ADDRESS)]),
    'static-range': ('''
@repr(C)
struct Bits { c_uint a : 3; c_int c : 7; };
const Bits HIGH = Bits { .a = 1u32 as c_uint, .c = 100i32 as c_int };
Bits wide = Bits { .a = 9u32 as c_uint, .c = 1i32 as c_int };
Bits edge = Bits { .a = 7u32 as c_uint, .c = -64i32 as c_int };
void local() {
    static const Bits low = Bits { .a = 0u32 as c_uint, .c = -65i32 as c_int };
    low as void;
}
''', [(4, 47, 'R-DIAG-CONST-001', 'R-FFI-0062', RANGE),
      (5, 20, 'R-DIAG-CONST-001', 'R-FFI-0062', RANGE),
      (8, 57, 'R-DIAG-CONST-001', 'R-FFI-0062', RANGE)]),
    'translation-time': ('''
@repr(C)
struct Bits { c_uint a : 3; c_int c : 7; };
Bits make(c_uint value) {
    Bits bits = Bits {};
    bits.a = value;
    return bits;
}
Bits build(c_int value) {
    return Bits { .a = 1u32 as c_uint, .c = value };
}
const Bits GOOD = make(7u32 as c_uint);
const Bits MADE = make(9u32 as c_uint);
const Bits BUILT = build(64i32 as c_int);
''', [(13, 19, 'R-DIAG-CONST-003', 'R-EXPR-0032',
       'translation-time evaluation panics (invalid_conversion) in `make`'),
      (14, 20, 'R-DIAG-CONST-003', 'R-EXPR-0032',
       'translation-time evaluation panics (invalid_conversion) in `build`')]),
}

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-c-layouts-') as directory:
        root = Path(directory)
        for name, (text, expected) in REJECTED.items():
            path = root / f'{name}.r'
            path.write_text(f'module layouts.{name.replace("-", "_")};\n' + text.lstrip('\n'))
            result = subprocess.run(
                [args.front, '--emit=hir', '--library-map', args.library_map, str(path)],
                capture_output=True, text=True, timeout=60)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = sorted(
                (int(m['line']), int(m['column']), m['code'], m['rule'], m['message'])
                for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m)
            assert found == sorted(expected), (name, found)
    print('C layout checks passed')


if __name__ == '__main__':
    main()
