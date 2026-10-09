#!/usr/bin/env python3
"""Check source-module borrow bounds, significant results and borrowed callable definitions."""

import argparse
from pathlib import Path
import subprocess
import tempfile


PROVIDER = '''module contracts.provider;
@generic<T> struct View { const T[] data; };
@generic<T> struct Pair { View<T> first; View<T> second; };
@generic<T> error Invalid { View<T> first; View<T> second; };
@must_use struct Ticket { i32 value; };
@must_use i32 answer() { return 42; }
@discardable bool toggle() { return true; }
@generic<T> View<T> view(const T[] data) { return View<T> {.data = data}; }
@generic<T> Pair<T> pair(const T[] a, const T[] b) {
    View<T> first = view(a); View<T> second = view(b);
    return Pair<T> {.first = first, .second = second};
}
@generic<T> void reject(const T[] a, const T[] b) throws Invalid<T> {
    View<T> first = view(a); View<T> second = view(b);
    throw Invalid<T> {.first = first, .second = second};
}
@generic<T> void relay(const T[] a, const T[] b) throws Invalid<T> {
    try { reject(a, b); } catch (Invalid<T> failure) { throw; }
}
constexpr str static_text() { return "stable"; }
const i32* select(bool first, const i32* a, const i32* b) {
    const i32* chosen = (first == true) ? a : b;
    return chosen;
}
'''

CLIENT = '''module contracts.client;
import contracts.provider;
i32 main() {
    i32[1] a = {20}; i32[1] b = {22};
    contracts.provider::Pair<i32> result = contracts.provider::pair(a, b);
    i32 value = contracts.provider::answer();
    try { contracts.provider::relay(a, b); }
    catch (contracts.provider::Invalid<i32> failure) {
        if (failure.first.data[0] + failure.second.data[0] != value) { result as void; return 1; }
    }
    return result.first.data[0] + result.second.data[0] - value;
}
'''


def emit_options(mode):
    """Options of one output; LLVM IR lowers every function, not only those main reaches, so
    that code generation also accepts the functions main never calls."""
    return ['--emit=' + mode, '--all-functions'] if mode == 'llvm-ir' else ['--emit=' + mode]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-borrowed-callables-') as directory:
        root = Path(directory)
        provider = root / 'provider.r'
        client = root / 'client.r'
        provider.write_text(PROVIDER)
        client.write_text(CLIENT)

        def emit(mode, paths):
            return subprocess.run([args.front, *emit_options(mode), *map(str, paths)],
                                  capture_output=True, text=True, timeout=30)

        for mode in ('interface', 'llvm-ir'):
            normal = emit(mode, [provider, client])
            reversed_order = emit(mode, [client, provider])
            assert normal.returncode == 0, (mode, normal.stderr)
            assert reversed_order.returncode == 0, (mode, reversed_order.stderr)
            assert normal.stdout == reversed_order.stdout, mode
            if mode == 'interface':
                interface = normal.stdout
                for name in ('pair', 'reject', 'relay'):
                    for line in interface.splitlines():
                        if 'name="contracts.provider::' + name in line:
                            assert 'origins=inputs parameters=(0 1) projections=union' in line, line
                answer = next(line for line in interface.splitlines()
                              if 'name="contracts.provider::answer"' in line)
                assert 'must_use=true' in answer
                assert 'discardable=true' not in answer
                toggle = next(line for line in interface.splitlines()
                              if 'name="contracts.provider::toggle"' in line)
                assert 'discardable=true' in toggle
                assert 'must_use=true' not in toggle
                assert 'borrow_contract=(return=(origins=none)' in answer
                assert 'origins=inputs parameters=(1 2)' in interface
                assert 'return_borrow_parameters=(1 2)' in interface

        cases = [
            ('generic-view-assignment-reserves-owner', """@generic<T> struct View { T[] data; };
            i32 grow(array<i32>* owner) throws std.array::push_error<i32> {
                std.array::push(owner, 2); return 3;
            }
            i32 bad() throws std.alloc::alloc_error, std.array::push_error<i32> {
                array<i32> owner=std.array::with_capacity::<i32>(1);
                std.array::push(&owner, 1);
                i32[] data=std.array::as_slice_mut(&owner);
                View<i32> view={.data=data};
                view.data[0]=grow(&owner); return 0;
            }""", 'R-DIAG-BORROW-001'),
            ('ordinary-slice-field-origin', """struct Held { const i32[] data; };
            Held pass(Held value) { return value; }
            i32 bad() { i32[1] local={42}; const i32[] data=local[0usize..1usize];
                Held input={.data=data}; Held output=pass(input);
                local[0]=0; return output.data[0]; }""", 'R-DIAG-BORROW-001'),
            ('consuming-closure-borrow-origin', """struct Held { own i32* owner; const i32[] data; };
            i32 bad() { i32[1] local={42}; const i32[] data=local[0usize..1usize];
                Held input={.owner=new i32(1), .data=data};
                fn once Held pass(Held value) { return move value; }
                Held output=pass(move input); local[0]=0; return output.data[0]; }""",
             'R-DIAG-BORROW-001'),
            ('unused-generic-resource-closure', """@generic<T> void unused(T value) {
                fn @noalloc once T invalid(T item) { own i32* scratch=new i32(1); return move item; }
            }""", 'R-DIAG-RESOURCE-001'),
            ('generic-move-local-borrow-escape', """@generic<T> struct Held { own i32* owner; const T[] data; };
            Held<i32> bad() { i32[1] local={42}; const i32[] data=local[0usize..1usize];
                Held<i32> result={.owner=new i32(1), .data=data}; return move result; }""",
             'R-DIAG-BORROW-002'),
            ('generic-empty-owners', """@generic<T> struct Pair { array<T> first; array<T> second; };
            @generic<T> Pair<T> Pair<T>::create() {
                array<T> a=std.array::create::<T>(); array<T> b=std.array::create::<T>();
                return Pair<T> {.first=move a, .second=move b};
            }
            i32 main() { Pair<i32> value=Pair<i32>::create(); drop value; return 0; }""", None),
            ('must-use-assignment', '@must_use i32 compute() { return 42; } void unused() { i32 value=0; value=compute(); }',
             'R-DIAG-USE-001'),
            ('must-use-await-result', '''@must_use async i32 compute() { return 42; }
            async void unused() throws std.async::start_error { i32 value=await compute(); }''',
             'R-DIAG-USE-001'),
            ('module-must-use-call', 'i32 main() { contracts.provider::answer(); return 0; }',
             'R-DIAG-USE-001'),
            ('module-must-use-binding', 'i32 main() { i32 value = contracts.provider::answer(); return 0; }',
             'R-DIAG-USE-001'),
            ('module-must-use-type', 'i32 main() { contracts.provider::Ticket value = {.value=1}; return 0; }',
             'R-DIAG-USE-001'),
            ('module-explicit-discard', 'i32 main() { contracts.provider::answer() as void; return 0; }', None),
            ('module-discardable-call', 'i32 main() { contracts.provider::toggle(); return 0; }', None),
            ('module-discardable-binding',
             'i32 main() { bool value = contracts.provider::toggle(); if (value == true) { return 0; } return 1; }',
             None),
            ('module-discardable-plain-call', 'i32 main() { contracts.provider::static_text(); return 0; }',
             'R-DIAG-TYPE-001'),
            ('generic-borrow-escape', '''contracts.provider::View<i32> bad() {
                i32[1] values = {42};
                contracts.provider::View<i32> result = contracts.provider::view(values);
                return result;
            }''', 'R-DIAG-BORROW-002'),
            ('generic-projected-mutation', '''i32 main() {
                i32[1] a={20}; i32[1] b={22};
                contracts.provider::Pair<i32> result=contracts.provider::pair(a,b);
                b[0]=0; return result.second.data[0];
            }''', 'R-DIAG-BORROW-001'),
            ('generic-error-mutation', '''i32 main() {
                i32[1] a={20}; i32[1] b={22};
                try { contracts.provider::relay(a,b); }
                catch (contracts.provider::Invalid<i32> failure) {
                    b[0]=0; return failure.second.data[0];
                }
                return 0;
            }''', 'R-DIAG-BORROW-001'),
            ('borrowed-closure-local-escape', '''void unused() {
                fn contracts.provider::View<i32> bad() {
                    i32[1] values={42}; const i32[] data=values[0usize..1usize]; return contracts.provider::View<i32> {.data=data};
                }
            }''', 'R-DIAG-BORROW-002'),
            ('borrowed-closure-suspend', '''async i32 child() { return 0; }
            async i32 run() throws std.async::start_error {
                i32[1] values={42};
                fn contracts.provider::View<i32> view(const i32[] data) {
                    return contracts.provider::View<i32> {.data=data};
                }
                contracts.provider::View<i32> result=view(values);
                i32 ignored=await child(); ignored as void; return result.data[0];
            }''', 'R-DIAG-ASYNC-001'),
            ('async-borrow-signature', '''void unused() {
                async fn i32 bad(const i32[] data) { return data[0]; }
            }''', 'R-DIAG-ASYNC-001'),
            ('invalid-async-predicate', '''@generic<F> void unused(F value) {
                @if (F is async fn shared() -> i32) { value as void; }
            }''', 'R-DIAG-META-001'),
            ('two-closed-borrowed-signatures', '''@generic<T, F: fn(const T[]) -> contracts.provider::View<T>>
            contracts.provider::View<T> apply(const F* f, const T[] data) {
                contracts.provider::View<T> result=f(data); return result;
            }
            i32 main() {
                i32[1] a={42}; u8[1] b={42u8};
                fn contracts.provider::View<i32> first(const i32[] data) {
                    return contracts.provider::View<i32> {.data=data};
                }
                fn contracts.provider::View<u8> second(const u8[] data) {
                    return contracts.provider::View<u8> {.data=data};
                }
                contracts.provider::View<i32> x=apply(&first,a);
                contracts.provider::View<u8> y=apply(&second,b);
                return x.data[0] - y.data[0] as i32;
            }''', None),
        ]
        # The receiver occupies parameter zero; the last input must survive into the high mask.
        wide_types = ', '.join(['const i32*'] * 64)
        wide_parameters = ', '.join(f'const i32* p{i}' for i in range(64))
        wide_arguments = ', '.join(f'p{i}' for i in range(64))
        wide_values = ', '.join(['&a'] * 63 + ['&b'])
        wide = (f'@generic<F: fn({wide_types}) -> const i32*> '
                f'const i32* apply(const F* f, {wide_parameters}) {{ '
                f'const i32* value=f({wide_arguments}); return value; }} '
                f'i32 main() {{ i32 a=1; i32 b=42; '
                f'fn const i32* last({wide_parameters}) {{ return p63; }} '
                f'const i32* result=apply(&last, {wide_values}); '
                'b=0; return *result; }')
        cases.append(('callable-high-origin-mask', wide, 'R-DIAG-BORROW-001'))
        for name, body, diagnostic in cases:
            client.write_text('module contracts.client;\nimport contracts.provider;\n' + body)
            result = emit('mir', [provider, client])
            assert result.returncode == (0 if diagnostic is None else 1), (name, result.stderr)
            if diagnostic is not None:
                assert diagnostic in result.stderr, (name, result.stderr)
    print('borrowed callables: module contracts, projected bounds, errors and lifetime checks passed')


if __name__ == '__main__':
    main()
