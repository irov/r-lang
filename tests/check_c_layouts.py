#!/usr/bin/env python3
"""Check the rejections of C layouts (Core R-AGG-0014, R-FFI-0061, R-FFI-0062, B9.3) and of C
atomic members (R-FFI-0017, B12): unions, packed and aligned aggregates, bit-fields and _Atomic
members, each diagnostic at its exact position with its code, rule and message."""

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
    # B12-1: an extern "C" block beside the functions leaves the module's bindings complete, so
    # an unresolved call is still diagnosed rather than reported as an unlowerable construct.
    'extern-block-neighbour': ('''
@repr(C)
union Value { c_int a; c_double b; };
extern "C" {
}
c_int read(Value v) {
    return missing_read(v);
}
''', [(7, 12, 'R-DIAG-NAME-001', 'R-NAME-0003',
       'called function name is unresolved at this source position')]),
    # R-FFI-0017 (B12): only a C-declared struct has atomic members, of C integers or raw
    # pointers; an R-declared @repr(C) struct has none.
    'atomic-members': ('''
@repr(C)
struct Local { atomic c_int count; };
@repr(C)
struct Fixed { atomic u32 count; };
''', [(3, 8, 'R-DIAG-FFI-003', 'R-FFI-0004',
       '@repr(C) struct fields require recursively compatible C ABI types and no drop'),
      (5, 8, 'R-DIAG-FFI-003', 'R-FFI-0004',
       '@repr(C) struct fields require recursively compatible C ABI types and no drop')]),
    # R-FFI-0022 (B12): @c_address names the object's address, a non-null raw pointer, and is
    # no thread_local import (R-FFI-0050).
    'c-address-forms': ('''
@link(name = "atomics", kind = "static")
@header("c_atomics.h")
@abi("atomics-abi")
extern "C" {
    @c_address
    raw c_int*? nullable_address;
    @c_address
    c_int value_address;
    @c_address
    thread_local raw c_int* local_address;
}
''', [(6, 5, 'R-DIAG-FFI-001', 'R-FFI-0022',
       '@c_address declares a non-null raw pointer to the C object'),
      (8, 5, 'R-DIAG-FFI-001', 'R-FFI-0022',
       '@c_address declares a non-null raw pointer to the C object'),
      (10, 5, 'R-DIAG-FFI-003', 'R-FFI-0050',
       'invalid, duplicate, or misplaced C import attribute')]),
}

# Rejections proven against fixtures/ffi/c_atomics.h: the block is verified, so the struct is
# whole and only its use is wrong. Line 1 is the module declaration.
HEADER_PRELUDE = '''
@link(name = "atomics", kind = "static")
@header("c_atomics.h")
@abi("atomics-abi")
extern "C" {
    @repr(C)
    @c_type(name = "atomic_counters", kind = "typedef")
    struct Counters {
        atomic c_uint64 hits;
        atomic c_int level;
        atomic raw void*? slot;
        atomic c_size total;
        atomic c_bool ready;
        COUNTERS_TAG;
    };
    @safety("COUNTERS-BUMP", "The pointer refers to a live value")
    c_uint64 atomic_counters_bump(raw Counters* counters, c_uint64 amount);
    IMPORT
}
'''
HEADER_REJECTED = {
    # An atomic member makes the struct a Move type: it passes through pointers only.
    'atomic-by-value': ('c_uchar tag', '''@safety("COUNTERS-VALUE", "None")
    c_uint64 atomic_counters_value(Counters counters);''', '''
c_uint64 read(Counters counters) {
    unsafe { return atomic_counters_value(move counters); }
}
''', [(18, 5, 'R-DIAG-FFI-001', 'R-FFI-0004', 'C import signature requires exact C ABI types')]),
    # The member is atomic in R but plain in the header.
    # The pointer of an address import has the object's C type, here struct atomic_registry.
    'c-address-mismatch': ('c_uchar tag', '''@c_address
    raw c_int* atomic_registry_main;''', '', [
        (19, 16, 'R-DIAG-FFI-004', 'R-FFI-0022', 'declaration disagrees with its verified C header')]),
    'atomic-not-in-header': ('atomic c_uchar tag', '', '', [
        (14, 24, 'R-DIAG-FFI-004', 'R-FFI-0017',
         'ABI record member type disagrees with the @repr(C) struct field type')]),
}

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', required=True)
    parser.add_argument('--fixtures', required=True)
    args = parser.parse_args()
    fixtures = Path(args.fixtures)
    sysroot = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], capture_output=True,
                             text=True, check=True).stdout.strip()
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
        for name, (tag, declaration, text, expected) in HEADER_REJECTED.items():
            path = root / f'{name}.r'
            prelude = HEADER_PRELUDE.replace('COUNTERS_TAG', tag).replace('IMPORT', declaration)
            path.write_text(f'module layouts.{name.replace("-", "_")};\n' + prelude.lstrip('\n') +
                            text.lstrip('\n'))
            result = subprocess.run(
                [args.front, '--emit=abi-record', '--library-map', args.library_map,
                 '--link-manifest', str(fixtures / 'codegen_ffi_c_atomics_link_manifest.json'),
                 '--sysroot', sysroot, '--abi-header-dir', str(fixtures / 'ffi'), str(path),
                 '--output', str(root / f'{name}.json')],
                capture_output=True, text=True, timeout=60)
            if result.returncode == 0:
                result = subprocess.run(
                    [args.front, '--emit=llvm-ir', '--library-map', args.library_map,
                     '--link-manifest', str(fixtures / 'codegen_ffi_c_atomics_link_manifest.json'),
                     '--sysroot', sysroot, '--abi-header-dir', str(fixtures / 'ffi'),
                     '--abi-record', str(root / f'{name}.json'), str(path)],
                    capture_output=True, text=True, timeout=60)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = sorted(
                (int(m['line']), int(m['column']), m['code'], m['rule'], m['message'])
                for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m)
            assert found == sorted(expected), (name, found)
    print('C layout checks passed')


if __name__ == '__main__':
    main()
